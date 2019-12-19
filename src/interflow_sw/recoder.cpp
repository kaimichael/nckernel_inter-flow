#include <cstdint>
#include <cstdlib>

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/interflowsw.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>
#include <nckernel/trace.h>

#include "../private.h"
#include "../util/rate.h"
#include "packet.h"
#include "common.h"

#include <rbufmgr.h>

#include <kodo_sliding_window/sliding_window_recoder.hpp>

typedef kodo_sliding_window::sliding_window_recoder recoder_t;
typedef recoder_t::factory factory_t;
typedef factory_t::pointer coder_t;
typedef recoder_t::header_type header_t;

struct nck_interflow_sw_rec {
	nck_interflow_sw_rec(coder_t coder, int ord) :
		coder(coder), source_size(coder->symbol_size()),
		coded_size(sizeof(struct interflow_sw_coded_packet) + coder->payload_size()),
		feedback_size(sizeof(struct interflow_sw_feedback_packet) + DIV_ROUND_UP(coder->symbols(), 8)),
		forward_code_window(coder->symbols() / 2), /* TODO: make configurable, possibly use a different default */
		flush(0), flush_next(0), flush_packet_no(0), order(ord), feedback(1),
		max_tx_attempts(UINT8_MAX), flush_attempts(0),
		has_source(0), has_feedback(0), timeout(), timeout_handle(), on_source_ready(), buffer(coder->block_size()),
		queue(coder->symbols() * coder->symbol_size()), queue_index(0), queue_length(0),
		last_packet_no(0), last_feedback_no(0), feedback_buffer(feedback_size)
	{
		nck_trigger_init(&on_source_ready);
		nck_trigger_init(&on_coded_ready);
		nck_trigger_init(&on_feedback_ready);
		coder->set_mutable_symbols(storage::storage(buffer));
		rbufmgr_init(&rbufmgr, coder->symbols(), 1);
		coder->set_trace_stdout();

		rate_control_credit_init(&rc, coder->symbols(), 0);

		memset(&stats, 0, sizeof(stats));
	}

	coder_t coder;

	size_t source_size, coded_size, feedback_size;
	size_t header_size;

	int forward_code_window;

	struct rbufmgr rbufmgr;
	uint32_t flush;
	uint8_t flush_next;
	uint32_t flush_packet_no;
	uint8_t order;

	// feedback mechanism
	uint32_t feedback;

	// rate control
	struct rate_control rc;


	uint8_t max_tx_attempts;
	uint8_t flush_attempts;

	int has_source;
	int has_feedback;

	struct nck_stats stats;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;
	struct nck_trigger on_coded_ready;

	std::vector<uint8_t> buffer;

	std::vector<uint8_t> queue;
	unsigned int queue_index;
	unsigned int queue_length;

	uint16_t last_packet_no;
	uint16_t last_feedback_no;

	std::vector<uint8_t> feedback_buffer;
};

char *nck_interflow_sw_rec_describe_packet(void *recoder, struct sk_buff *packet);
struct nck_stats *nck_interflow_sw_rec_get_stats(void *recoder);

NCK_RECODER_IMPL(nck_interflow_sw, NULL, nck_interflow_sw_rec_describe_packet, nck_interflow_sw_rec_get_stats)

static void recoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		struct nck_interflow_sw_rec *recoder = (struct nck_interflow_sw_rec *)context;

		recoder->stats.s[NCK_STATS_TIMER_FLUSH]++;

		if (!nck_interflow_sw_rec_has_source(recoder)) {
			nck_interflow_sw_rec_flush_source(recoder);
		}
		if (!nck_interflow_sw_rec_has_coded(recoder)) {
			nck_interflow_sw_rec_flush_coded(recoder);
		}
	}
}

EXPORT
struct nck_interflow_sw_rec *nck_interflow_sw_rec(uint32_t symbols, uint32_t symbol_size, struct nck_timer *timer, const struct timeval *timeout)
{
	uint8_t ord = 0;
	while (symbols > (1U<<ord))
		++ord;

	if (symbols != (1U<<ord)) {
		// TODO: raise a proper error
		fprintf(stderr, "Number of symbols must be a power of 2\n");
		return NULL;
	}

	factory_t factory(fifi::api::field::binary8, symbols, symbol_size);

	struct nck_interflow_sw_rec *result = new struct nck_interflow_sw_rec(factory.build(), ord);
	result->header_size = 4+1;

	if (timer) {
		assert(timeout != NULL);
		result->timeout = *timeout;
		result->timeout_handle = nck_timer_add(timer, NULL, result, recoder_timeout_flush);
	}

	return result;
}

EXPORT
void nck_interflow_sw_rec_set_redundancy(struct nck_interflow_sw_rec *recoder, int32_t redundancy)
{
	auto coder = recoder->coder;
	int32_t effective = max_t(int32_t, redundancy, -coder->symbols());

	/* the credit counter is never allowed to be decreased with any
	 * new innovative coded packet. And an innovative packet would
	 * increase the credit counter by: coder->symbols() + redundancy
	 *
	 * The minimum allowed redundancy is therefore -coder->symbols()
	 */
	rate_control_credit_change(&recoder->rc, effective);
}

EXPORT
void nck_interflow_sw_rec_set_feedback(struct nck_interflow_sw_rec *recoder, uint32_t enable)
{
	recoder->feedback = enable;
}

EXPORT
void nck_interflow_sw_rec_set_tx_attempts(struct nck_interflow_sw_rec *recoder, uint8_t tx_attempts)
{
	/* TODO only enable tx_attemps > 1 when feedback is enabled? */
	recoder->max_tx_attempts = tx_attempts;
}

EXPORT
void nck_interflow_sw_rec_set_forward_code_window(struct nck_interflow_sw_rec *recoder, uint32_t forward_code_window)
{
	recoder->forward_code_window = forward_code_window;
}

EXPORT
void nck_interflow_sw_rec_free(struct nck_interflow_sw_rec *recoder)
{
	if (recoder->timeout_handle) {
		nck_timer_cancel(recoder->timeout_handle);
		nck_timer_free(recoder->timeout_handle);
	}
	delete recoder;
}

EXPORT
int nck_interflow_sw_rec_has_source(struct nck_interflow_sw_rec *recoder)
{
	return recoder->queue_length != recoder->queue_index || recoder->has_source;
}

EXPORT
int nck_interflow_sw_rec_complete(struct nck_interflow_sw_rec *recoder)
{
	UNUSED(recoder);
	// TODO: how do we know we are complete?
	return 0;
}

static void move_to_next_source(struct nck_interflow_sw_rec *recoder)
{
	auto coder = recoder->coder;

	//  1. `index` is the next symbol that will be output
	//  2. `sequence` points just after the last known symbol
	//  3. `flush` marks the point up until we must get the packets out
	if (rbufmgr_empty(&recoder->rbufmgr)) {
		assert(!recoder->has_source);
		return;
	}

	// we move the `index` further until:
	//  1. we found a symbol that we can decode
	//       recoder->has_source
	//  2. we cannot decode the symbol but we do not need to flush it
	//       !recoder->has_source && recoder->flush != index
	while (!rbufmgr_empty(&recoder->rbufmgr)) {

		uint32_t pos = rbufmgr_peek(&recoder->rbufmgr);
		recoder->has_source = coder->is_symbol_uncoded(pos) || coder->check_symbol_status(pos);

		if (recoder->has_source) {
			if (recoder->flush == rbufmgr_read_seqno(&recoder->rbufmgr)) {
				// we move the flush further so that flush is always after
				// index if we can call get_source
				recoder->flush += 1;
			}
			break;
		} else {
			if (recoder->flush == rbufmgr_read_seqno(&recoder->rbufmgr))
				break;

			/* consume undecodable */
			rbufmgr_read(&recoder->rbufmgr);
		}
	}


	if (rbufmgr_empty(&recoder->rbufmgr))
		recoder->has_source = 0;
}

EXPORT
void nck_interflow_sw_rec_flush_coded(struct nck_interflow_sw_rec *recoder)
{
	auto coder = recoder->coder;

	rate_control_flush(&recoder->rc);

	if (_has_coded(recoder))
		nck_trigger_call(&recoder->on_coded_ready);
}

EXPORT
void nck_interflow_sw_rec_flush_source(struct nck_interflow_sw_rec *recoder)
{
	auto coder = recoder->coder;
	uint32_t sequence = coder->sequence_number();

	if (rbufmgr_empty(&recoder->rbufmgr))
		return;

	// we move the flush pointer to the end
	recoder->flush = sequence;
	if (!recoder->has_source) {
		move_to_next_source(recoder);
	}

	if (nck_interflow_sw_rec_has_source(recoder)) {
		nck_trigger_call(&recoder->on_source_ready);
	}
}

/**
 * nck_sw_rec_apply_forward_window - enable/disable symbols in the forward_window
 * @recoder: recoder structure that will be used
 * @distance: distance by which the window was moved
 *
 * Enables new symbols in the forward window, and disables symbols which have been pushed
 * out of the forward window
 */
static inline void nck_interflow_sw_rec_apply_forward_window(struct nck_interflow_sw_rec *recoder, uint32_t distance)
{
	auto coder = recoder->coder;
	uint32_t symbols = coder->symbols();
	uint32_t local_seqno = coder->sequence_number();
	uint32_t s;
	int i, enabled_symbols, disabled_symbols;

	if (distance >= symbols)
		distance = symbols;
	enabled_symbols = min_t(uint32_t, distance, recoder->forward_code_window);
	/* enable the new symbols */
	for (s = local_seqno - enabled_symbols; s != local_seqno; s++) {
		i = s % symbols;
		coder->nested()->enable_symbol(i);
	}

	/* disable the symbols which moved out of the forwarding window */
	disabled_symbols = min_t(uint32_t, distance, symbols - recoder->forward_code_window);
	for (s = local_seqno - recoder->forward_code_window - disabled_symbols;
	     s != local_seqno - recoder->forward_code_window; s++) {
		i = s % symbols;
		coder->nested()->disable_symbol(i);
	}
}

/**
 * nck_sw_rec_put_coded_consume_old - get old source symbols and inform
 *  consumer/copy them to queue
 *
 * @recoder: recoder structure that will be used
 * @sequence: received sequence number in packet
 * @symbols: number of symbols in recoder
 *
 * The consumer will be informed about source symbols which will be lost after
 * the new packet was inserted into the recoder. The remaining source symbols
 * (after the consumer was triggered) will be copied to the queue.
 */
static void nck_interflow_sw_rec_put_coded_consume_old(struct nck_interflow_sw_rec *recoder,
					     uint32_t sequence,
					     uint32_t symbols)
{
	auto coder = recoder->coder;
	size_t i;
	size_t read_index;
	size_t lost_read_blocks;
	uint32_t pos;
	uint32_t symbol_size = coder->symbol_size();
	size_t undecodable_symbols = 0;

	/* is it outside our current window and to old to be a new seqno?
	 * then we have nothing which we have to save
	 */
	if (rbufmgr_outdated(&recoder->rbufmgr, sequence))
		return;

	/* if we advanced, tell the rate control. */
	rate_control_advance(&recoder->rc, rbufmgr_write_distance(&recoder->rbufmgr, sequence));

	/* get number of entries which will be lost after inserting */
	rbufmgr_shift_distance(&recoder->rbufmgr, sequence, &read_index,
			       &lost_read_blocks);

	/* nothing to save when nothing will get shifted outside the window */
	if (lost_read_blocks <= 0)
		return;

	/* everything will be lost which is outside the current window.
	 * the flush ("read till this seqno") must therefore be right
	 * at the beginning of the new window
	 */
	recoder->flush = sequence - symbols;

	/* find first entry with source which will be lost */
	for (i = 0; i < lost_read_blocks; i++) {
		pos = rbufmgr_peek(&recoder->rbufmgr);

		/* ignore undecoded symbols and "consume" this index */
		if (!coder->is_symbol_uncoded(pos) &&
		    !coder->check_symbol_status(pos)) {
			rbufmgr_read(&recoder->rbufmgr);
			undecodable_symbols++;
			continue;
		}

		recoder->has_source = 1;
		break;
	}

	lost_read_blocks -= undecodable_symbols;

	/* inform consumer about new entries */
	if (_has_source(recoder))
		nck_trigger_call(&recoder->on_source_ready);

	/* reset the queue
	 * if there is something inside it is the callers own fault
	 */
	recoder->queue_length = 0;
	recoder->queue_index = 0;

	/* copy entries which will be lost to queue */
	for (i = 0; i < lost_read_blocks; i++) {
		pos = rbufmgr_read(&recoder->rbufmgr);

		/* check whether it is a source symbol
		 */
		if (!coder->is_symbol_uncoded(pos) &&
		    !coder->check_symbol_status(pos))
			continue;

		/* don't overflow queue */
		assert(recoder->queue_length < coder->symbols());

		/* copy symbol to queue */
		auto src = &recoder->buffer[pos * symbol_size];
		auto dst = &recoder->queue[recoder->queue_length * symbol_size];
		memcpy(dst, src, symbol_size);
		recoder->queue_length += 1;
	}

	/* TODO this should not be possible because it first sets
	 * recoder->flush = sequence - symbols;
	 */
	// Now it is possible that we skipped a full rotation of the symbols.
	// As a result the flushing procedure above terminates at a point which
	// is still outside the new decoding window.
	// Here we first detect this case and handle it by placing the index
	// above the first symbol of the new window.
	if (coder->sequence_compare(rbufmgr_read_seqno(&recoder->rbufmgr) + symbols, sequence) < 0)
		recoder->flush = rbufmgr_read_seqno(&recoder->rbufmgr);

	// force a recheck of has_source
	recoder->has_source = 0;
}

EXPORT
int nck_interflow_sw_rec_put_coded(struct nck_interflow_sw_rec *recoder, struct sk_buff *packet)
{
	struct interflow_sw_coded_packet *interflow_sw_coded_packet;
	auto coder = recoder->coder;
	uint32_t symbols = coder->symbols();
	uint32_t pos, previous_seqno;
	uint16_t packet_no;
	int32_t packet_no_diff;
	int read_payload_retcode;

	if (!pskb_may_pull(packet, sizeof(*interflow_sw_coded_packet)))
		return -1;

	interflow_sw_coded_packet = (struct interflow_sw_coded_packet *)packet->data;
	skb_pull(packet, sizeof(*interflow_sw_coded_packet));

	if (interflow_sw_coded_packet->packet_type != INTERFLOW_SW_PACKET_TYPE_CODED)
		return -1;

	/* should hold a least the sequence number + systematic flag */
	if (!pskb_may_pull(packet, recoder->header_size))
		return -1;

	recoder->stats.s[NCK_STATS_PUT_CODED]++;

	packet_no = ntohs(interflow_sw_coded_packet->packet_no);

	header_t header;
	recoder->coder->read_header(packet->data, header);

	/* update last_packet_no, but only when going forward or resetting */
	packet_no_diff = packet_no - recoder->last_packet_no;
	if ((packet_no_diff > 0) || (packet_no_diff < -((int32_t)symbols)))
		recoder->last_packet_no = packet_no;

	/* try to let the consumer get remaining symbols or copy source symbols
	 * to queue for later
	 */
	nck_interflow_sw_rec_put_coded_consume_old(recoder, header.sequence, symbols);

	// pad short packets with zeros before giving to the decoder
	skb_put_zeros(packet, coder->payload_size());

	previous_seqno = coder->sequence_number();
	read_payload_retcode = coder->read_payload(packet->data);
	switch (read_payload_retcode) {
	case READ_PAYLOAD_REDUNDANT:
		recoder->stats.s[NCK_STATS_PUT_CODED_REDUNDANT]++;
		break;
	case READ_PAYLOAD_INNOVATIVE:
		recoder->stats.s[NCK_STATS_PUT_CODED_INNOVATIVE]++;
		break;
	case READ_PAYLOAD_OUTDATED:
		recoder->stats.s[NCK_STATS_PUT_CODED_OUTDATED]++;
		break;
	case READ_PAYLOAD_CONFLICT:
		recoder->stats.s[NCK_STATS_PUT_CODED_CONFLICT]++;
		break;
	}
	rbufmgr_insert(&recoder->rbufmgr, header.sequence);
	nck_interflow_sw_rec_apply_forward_window(recoder, coder->sequence_number() - previous_seqno);

	/* TODO: only flush if target rate > 0? The current behaviour is that even
	 * "silent" recoders transport the flush. */
	if (interflow_sw_coded_packet->flags & INTERFLOW_SW_CODED_PACKET_FLUSH &&
	    ((int32_t)(recoder->last_packet_no - recoder->flush_packet_no) > 0) &&
	    !recoder->flush_next) {
		/* on a flush, definitely send a packet */
		rate_control_insert(&recoder->rc, true);

		recoder->flush_next = interflow_sw_coded_packet->flags & INTERFLOW_SW_CODED_PACKET_FLUSH;
		recoder->flush_packet_no = recoder->last_packet_no;
	} else if (read_payload_retcode == READ_PAYLOAD_INNOVATIVE) {
		rate_control_insert(&recoder->rc, false);
		/* allow flushing again */
		recoder->flush_attempts = max_t(uint8_t, recoder->max_tx_attempts, 1) - 1;
	}

	if (rbufmgr_empty(&recoder->rbufmgr)) {
		assert(!recoder->has_source);
	} else {
		pos = rbufmgr_peek(&recoder->rbufmgr);
		recoder->has_source = coder->is_symbol_uncoded(pos) || coder->check_symbol_status(pos);

		if (recoder->has_source &&
		    recoder->flush == rbufmgr_read_seqno(&recoder->rbufmgr)) {
			// if we have a source symbol then flush should be somewhere after that
			recoder->flush += 1;
		}
	}

	if (recoder->flush != coder->sequence_number() && recoder->timeout_handle) {
		nck_timer_rearm(recoder->timeout_handle, &recoder->timeout);
	}

	if (nck_interflow_sw_rec_has_source(recoder)) {
		nck_trigger_call(&recoder->on_source_ready);
	}

	if (nck_interflow_sw_rec_has_coded(recoder))
		nck_trigger_call(&recoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_interflow_sw_rec_has_coded(struct nck_interflow_sw_rec *recoder)
{
	/* there are no source symbols (0) - all credit will therefore be used
	 * for repair packets
	 */
	return rate_control_next_repair(&recoder->rc, 0);
}

/**
 * nck_sw_check_zero_coefficients - check whether the payload has only zero coefficients
 * @coder: coder object to check with
 * @payload: payload data to check for
 *
 * Returns true if this is a zero-only coefficient packet, false otherwise
 */
static bool nck_interflow_sw_check_zero_coefficients(coder_t coder, uint8_t *payload)
{
	int symbols = coder->symbols();
	header_t header;

	coder->read_header(payload, header);
	if (header.systematic_flag != 0)
		return false;

	for (int i = 0; i < symbols; i++)
		if (header.coefficients[i] != 0)
			return false;

	return true;
}

EXPORT
int nck_interflow_sw_rec_get_coded(struct nck_interflow_sw_rec *recoder, struct sk_buff *packet)
{
	assert(nck_interflow_sw_rec_has_coded(recoder));

	struct interflow_sw_coded_packet *interflow_sw_coded_packet;
	auto coder = recoder->coder;

	/* step will always produce repair packet because we have 0 source
	 * packets
	 */
	rate_control_step(&recoder->rc, 0);

	skb_reserve(packet, sizeof(*interflow_sw_coded_packet));

	size_t payload_size = coder->payload_size();
	uint8_t *payload = (uint8_t *)skb_put(packet, payload_size);
	size_t real_size = coder->write_payload(payload);
	if (nck_interflow_sw_check_zero_coefficients(coder, payload)) {
		recoder->stats.s[NCK_STATS_GET_CODED_DISCARDED_ZERO_ONLY]++;
		return -1;
	}

	recoder->stats.s[NCK_STATS_GET_CODED]++;

	assert(payload_size >= real_size);
	skb_trim(packet, payload_size - real_size);
	skb_trim_zeros(packet);

	/* do not trim off the minimal header as well. */
	if (packet->len < recoder->header_size) {
		skb_put(packet, recoder->header_size - packet->len);
	}

	interflow_sw_coded_packet = (struct interflow_sw_coded_packet *)skb_push(packet, sizeof(*interflow_sw_coded_packet));
	memset(interflow_sw_coded_packet, 0, sizeof(*interflow_sw_coded_packet));
	interflow_sw_coded_packet->packet_type = INTERFLOW_SW_PACKET_TYPE_CODED;
	interflow_sw_coded_packet->order = recoder->order;
	interflow_sw_coded_packet->packet_no = htons(recoder->last_packet_no);

	if (recoder->flush_next) {
		interflow_sw_coded_packet->flags |= INTERFLOW_SW_CODED_PACKET_FLUSH;
		interflow_sw_coded_packet->packet_no = htons(recoder->flush_packet_no);
		recoder->flush_next = 0;
	}

	return 0;
}

EXPORT
char *nck_interflow_sw_rec_describe_packet(void *rec, struct sk_buff *packet)
{
	struct nck_interflow_sw_rec *recoder = (struct nck_interflow_sw_rec*)rec;

	return nck_interflow_sw_common_describe_packet(packet, recoder->coder->symbols());
}

EXPORT
struct nck_stats *nck_interflow_sw_rec_get_stats(void *rec)
{
	struct nck_interflow_sw_rec *recoder = (struct nck_interflow_sw_rec*)rec;

	return &recoder->stats;
}

EXPORT
int nck_interflow_sw_rec_get_source(struct nck_interflow_sw_rec *recoder, struct sk_buff *packet)
{
	auto coder = recoder->coder;
	uint32_t symbol_size = coder->symbol_size();
	uint8_t *symbol, *payload;

	recoder->stats.s[NCK_STATS_GET_SOURCE]++;

	if (recoder->queue_length != recoder->queue_index) {
		// here we get a packet from the queue
		symbol = &recoder->queue[recoder->queue_index*symbol_size];
		recoder->queue_index += 1;
	} else {
		// here the queue is empty
		// but we should have something in the recoder
		assert(recoder->has_source);

		uint32_t pos = rbufmgr_read(&recoder->rbufmgr);
		symbol = &recoder->buffer[pos * symbol_size];
		recoder->has_source = 0;

		move_to_next_source(recoder);
	}

	payload = (uint8_t *)skb_put(packet, symbol_size);
	memcpy(payload, symbol, symbol_size);

	return 0;
}

/**
 * nck_sw_rec_schedule_retransmission - schedule retransmission for feedback
 * @data: pointer to the feedback buffer
 *
 * Schedules the number of required retransmissions to satisfy the feedback
 * sender, at least to our capabilities (i.e. how depending on how many
 * coded symbols are buffered).
 */
static inline void nck_interflow_sw_rec_schedule_retransmission(struct nck_interflow_sw_rec *recoder, uint8_t *data, uint32_t feedback_seqno)
{
	auto coder = recoder->coder;
	int symbols = coder->symbols();
	int rank = coder->rank();
	int i, count;

	UNUSED(feedback_seqno);

	/* TODO: this can be implemented in various ways. We may want to make this
	 * configurable from outside, and switch() between various ways. For now,
	 * just use a stupid resender which sends as much as the feedback is missing,
	 * regardless of whether we even have the needed information. We do not send
	 * more than our own rank, though. */
	count = 0;
	for (i = 0; i < symbols; i++) {
		if ((data[i/8] & (1 << (i%8))) == 0)
			continue;

		rate_control_insert(&recoder->rc, true);

		count++;
		if (count > rank)
			break;
	}
}

/**
 * nck_sw_rec_apply_feedback - apply the feedback by enabling/disabling symbosl accordingly
 * @recoder: recoder structure that will be used
 * @data: pivot bitmap from the feedback
 * @feedback_seqno: sequence number of the feedback
 */
static inline void nck_interflow_sw_rec_apply_feedback(struct nck_interflow_sw_rec *recoder, uint8_t *data, uint32_t feedback_seqno)
{
	auto coder = recoder->coder;
	int symbols = coder->symbols();
	uint32_t local_seqno = coder->sequence_number();
	uint32_t check_start, check_end, s;
	int32_t seqno_dist;
	int i;

	/* TODO: calling stuff with ->nested()-> is not clean, but my C++ code-fu is
	 * too limited to make this better ... */

	seqno_dist = feedback_seqno - local_seqno;

	if (seqno_dist > 0) {
		/* the recoder should move forward with its sequence number
		 * if the feedback sequence number is newer than the local sequence number */
		nck_interflow_sw_rec_put_coded_consume_old(recoder, feedback_seqno, symbols);
		nck_interflow_sw_rec_apply_forward_window(recoder, feedback_seqno);
		coder->increment_sequence(seqno_dist);
		local_seqno = feedback_seqno;
		seqno_dist = 0;
	}

	/* feedback is too old. This is possibly an error case, do not continue */
	if (-seqno_dist >= symbols)
		return;

	if (seqno_dist < 0) {
		/* feedback is older, start parsing the feedback at the beginning
		 * of the local window */
		check_start = local_seqno - symbols;
		check_end = feedback_seqno;
	} else {
		/* feedback is newer, start parsing the feedback at the end of
		 * the feedback string */
		check_start = feedback_seqno - symbols;
		check_end = local_seqno;
	}

	/* check this for off-by-one problems ... */
	for (s = check_start; s != check_end; ++s) {
		i = s % symbols;
		/* enable/disable symbols */
		if ((data[i/8] & (1 << (i%8))) == 0) {
			/* index was acknowledged, disable it */
			coder->nested()->disable_symbol(i);
		} else {
			/* index is missing, enable it */
			coder->nested()->enable_symbol(i);
		}
	}

}

EXPORT
int nck_interflow_sw_rec_put_feedback(struct nck_interflow_sw_rec *recoder, struct sk_buff *packet)
{
	auto coder = recoder->coder;
	int symbols = coder->symbols();
	struct interflow_sw_feedback_packet *interflow_sw_feedback_packet;
	int32_t feedback_no_diff;
	uint32_t feedback_sequence;
	uint16_t feedback_no;

	// check length
	if (!pskb_may_pull(packet, recoder->feedback_size))
		return -1;

	recoder->stats.s[NCK_STATS_PUT_FEEDBACK]++;

	/* record the feedback to send it out later again */
	interflow_sw_feedback_packet = (struct interflow_sw_feedback_packet *)packet->data;
	skb_pull(packet, sizeof(*interflow_sw_feedback_packet));

	if (interflow_sw_feedback_packet->packet_type != INTERFLOW_SW_PACKET_TYPE_FEEDBACK)
		return -1;

	feedback_no = ntohs(interflow_sw_feedback_packet->feedback_no);
	feedback_sequence = ntohl(interflow_sw_feedback_packet->sequence);

	/* ignore feedback which we already had or which are slightly older */
	feedback_no_diff = feedback_no - recoder->last_feedback_no;

	/* TODO: handling resets globally would be a more clean way to do */
	if ((-symbols < feedback_no_diff) && (feedback_no_diff <= 0))
		return 0;

	nck_interflow_sw_rec_apply_feedback(recoder, packet->data, feedback_sequence);

	/* copy the feedback to send it out again */
	memcpy(&recoder->feedback_buffer[0], interflow_sw_feedback_packet, recoder->feedback_size);
	recoder->last_feedback_no = feedback_no;
	recoder->has_feedback = 1;
	nck_trigger_call(&recoder->on_feedback_ready);

	nck_interflow_sw_rec_schedule_retransmission(recoder, packet->data, feedback_sequence);

	return 0;
}

EXPORT
int nck_interflow_sw_rec_get_feedback(struct nck_interflow_sw_rec *recoder, struct sk_buff *packet)
{
	assert(recoder->has_feedback);

	recoder->stats.s[NCK_STATS_GET_FEEDBACK]++;

	/* copy the last received, buffered feedback */
	skb_put(packet, recoder->feedback_size);
	memcpy(packet->data, &recoder->feedback_buffer[0], recoder->feedback_size);

	recoder->has_feedback = 0;

	return 0;
}

EXPORT
int nck_interflow_sw_rec_has_feedback(struct nck_interflow_sw_rec *recoder)
{
	return recoder->has_feedback;
}
