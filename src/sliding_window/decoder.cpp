#include <cstdint>
#include <cstdlib>

#include <cerrno>
#include <cstring>

#include <sys/time.h>
#include <arpa/inet.h>
#include <linux/types.h>

#include <kodo_core/mutable_shallow_storage_layers.hpp>

#include <nckernel/sw.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>
#include <nckernel/trace.h>

#include <kodo_sliding_window/sliding_window_decoder.hpp>
#include <kodo_sliding_window/band_matrix_decoder_layers.hpp>
#include <kodo_sliding_window/header_type.hpp>

#include "../private.h"
#include "packet.h"
#include "common.h"

#include <rbufmgr.h>

typedef kodo_sliding_window::sliding_window_decoder::factory factory_t;
typedef factory_t::pointer coder_t;
typedef kodo_sliding_window::header_type<uint32_t, uint8_t> header_t;

struct nck_sw_dec {
	nck_sw_dec(coder_t coder, int ord) :
		coder(coder), source_size(coder->symbol_size()),
		coded_size(sizeof(struct sw_coded_packet) + coder->payload_size()),
		feedback_size(sizeof(struct sw_feedback_packet) + DIV_ROUND_UP(coder->symbols(), 8)),
		initialized(0), flush(0), order(ord), feedback(1), has_source(0), has_feedback(0), feedback_packet_no(0), feedback_no(0),
		max_feedback_tx_attempts(UINT8_MAX), feedback_tx_attempts(0),
		timeout(), timeout_handle(), fb_timeout(), fb_timeout_handle(NULL),
		on_source_ready(), buffer(coder->block_size()),
		queue(coder->symbols() * coder->symbol_size()), queue_index(0), queue_length(0)
	{
		nck_trigger_init(&on_source_ready);
		nck_trigger_init(&on_feedback_ready);
		coder->set_mutable_symbols(storage::storage(buffer));
		rbufmgr_init(&rbufmgr, coder->symbols(), 1);
		//coder->set_trace_stdout();

		memset(&stats, 0, sizeof(stats));
	}

	coder_t coder;

	size_t source_size, coded_size, feedback_size;
	size_t header_size;

	int initialized;
	struct rbufmgr rbufmgr;
	uint32_t flush;
	uint8_t order;

	// feedback mechanism
	uint32_t feedback;

	int has_source;
	int has_feedback;
	uint16_t feedback_packet_no;
	uint16_t feedback_no;

	uint8_t max_feedback_tx_attempts;
	uint8_t feedback_tx_attempts;

	struct nck_stats stats;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	/* feedback timeout */
	struct timeval fb_timeout;
	struct nck_timer_entry *fb_timeout_handle;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	std::vector<uint8_t> buffer;

	std::vector<uint8_t> queue;
	unsigned int queue_index;
	unsigned int queue_length;
};

char *nck_sw_dec_describe_packet(void *decoder, struct sk_buff *packet);
struct nck_stats *nck_sw_dec_get_stats(void *decoder);

NCK_DECODER_IMPL(nck_sw, NULL, nck_sw_dec_describe_packet, nck_sw_dec_get_stats)

EXPORT
void nck_sw_dec_set_sequence(struct nck_sw_dec *decoder, uint32_t sequence)
{
	decoder->initialized = 1;
	decoder->flush = sequence - 1;
}


EXPORT
void nck_sw_dec_set_fb_timeout(struct nck_sw_dec *decoder, struct timeval *fb_timeout)
{
	if (!fb_timeout)
		return;
	assert(decoder->fb_timeout_handle);
	if (!decoder->fb_timeout_handle)
		return;

	decoder->fb_timeout = *fb_timeout;

	if (!timerisset(fb_timeout)) {
		nck_timer_cancel(decoder->fb_timeout_handle);
		return;
	}

	decoder->feedback_tx_attempts = 0;

	if (nck_timer_pending(decoder->fb_timeout_handle))
		nck_timer_rearm(decoder->fb_timeout_handle, &decoder->fb_timeout);

}

static void decoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		struct nck_sw_dec *decoder = (struct nck_sw_dec *)context;
		_flush_source(decoder);
		decoder->stats.s[NCK_STATS_TIMER_FLUSH]++;
	}
}

/**
 * decoder_fb_timeout - send feedback after timeout
 * @entry: timer entry
 * @context: pointer to decoder context
 * @success: whether this is a regular call (set) or cancel etc (unset)
 *
 * schedules a feedback after a timeout - can be used to regularly send
 * feedbacks.
 */
static void decoder_fb_timeout(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		struct nck_sw_dec *decoder = (struct nck_sw_dec *)context;

		if (decoder->feedback_tx_attempts >= decoder->max_feedback_tx_attempts) {
			decoder_timeout_flush(entry, context, success);
			return;
		}

		decoder->feedback_tx_attempts++;
		decoder->has_feedback = 1;
		nck_trigger_call(&decoder->on_feedback_ready);
		decoder->stats.s[NCK_STATS_TIMER_FB_FLUSH]++;
	}
}

EXPORT
struct nck_sw_dec *nck_sw_dec(uint32_t symbols, uint32_t symbol_size, struct nck_timer *timer, const struct timeval *timeout, const char *matrix_form)
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
	coder_t coder = factory.build();

	struct nck_sw_dec *result = new struct nck_sw_dec(coder, ord);
	result->header_size = 4+1;

	if (timeout && timerisset(timeout)) {
		assert(timer != NULL);
		result->timeout = *timeout;
		result->timeout_handle = nck_timer_add(timer, NULL, result, decoder_timeout_flush);
	}

	if (timer)
		result->fb_timeout_handle = nck_timer_add(timer, NULL, result, decoder_fb_timeout);

	return result;
}

EXPORT
void nck_sw_dec_set_feedback(struct nck_sw_dec *decoder, uint32_t enable)
{
	decoder->feedback = enable;
}

EXPORT
void nck_sw_dec_set_tx_attempts(struct nck_sw_dec *decoder, uint8_t tx_attempts)
{
	decoder->max_feedback_tx_attempts = tx_attempts;
}

EXPORT
void nck_sw_dec_free(struct nck_sw_dec *decoder)
{
	if (decoder->timeout_handle) {
		nck_timer_cancel(decoder->timeout_handle);
		nck_timer_free(decoder->timeout_handle);
	}

	if (decoder->fb_timeout_handle) {
		nck_timer_cancel(decoder->fb_timeout_handle);
		nck_timer_free(decoder->fb_timeout_handle);
	}

	delete decoder;
}

EXPORT
int nck_sw_dec_has_source(struct nck_sw_dec *decoder)
{
	return decoder->queue_length != decoder->queue_index || decoder->has_source;
}

EXPORT
int nck_sw_dec_complete(struct nck_sw_dec *decoder)
{
	// if there are no pending symbols known, we consider ourselves complete.
	return rbufmgr_empty(&decoder->rbufmgr);
}

static void move_to_next_source(struct nck_sw_dec *decoder)
{
	auto coder = decoder->coder;

	//  1. `index` is the next symbol that will be output
	//  2. `sequence` points just after the last known symbol
	//  3. `flush` marks the point up until we must get the packets out
	if (rbufmgr_empty(&decoder->rbufmgr)) {
		assert(!decoder->has_source);
		return;
	}

	// we move the `index` further until:
	//  1. we found a symbol that we can decode
	//       decoder->has_source
	//  2. we cannot decode the symbol but we do not need to flush it
	//       !decoder->has_source && decoder->flush != index
	while (!rbufmgr_empty(&decoder->rbufmgr)) {

		uint32_t pos = rbufmgr_peek(&decoder->rbufmgr);
		decoder->has_source = coder->is_symbol_uncoded(pos) || coder->check_symbol_status(pos);

		if (decoder->has_source) {
			if (decoder->flush == rbufmgr_read_seqno(&decoder->rbufmgr)) {
				// we move the flush further so that flush is always after
				// index if we can call get_source
				decoder->flush += 1;
			}
			break;
		} else {
			if (decoder->flush == rbufmgr_read_seqno(&decoder->rbufmgr))
				break;

			/* consume undecodable */
			rbufmgr_read(&decoder->rbufmgr);
		}
	}


	if (rbufmgr_empty(&decoder->rbufmgr))
		decoder->has_source = 0;
}

EXPORT
void nck_sw_dec_flush_source(struct nck_sw_dec *decoder)
{
	auto coder = decoder->coder;
	uint32_t sequence = coder->sequence_number();

	nck_timer_cancel(decoder->fb_timeout_handle);

	if (rbufmgr_empty(&decoder->rbufmgr))
		return;

	// we move the flush pointer to the end
	decoder->flush = sequence;
	if (!decoder->has_source) {
		move_to_next_source(decoder);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}
}

/**
 * nck_sw_dec_put_coded_consume_old - get old source symbols and inform
 *  consumer/copy them to queue
 *
 * @decoder: decoder structure that will be used
 * @sequence: received sequence number in packet
 * @symbols: number of symbols in decoder
 *
 * The consumer will be informed about source symbols which will be lost after
 * the new packet was inserted into the decoder. The remaining source symbols
 * (after the consumer was triggered) will be copied to the queue.
 */
static void nck_sw_dec_put_coded_consume_old(struct nck_sw_dec *decoder,
					     uint32_t sequence,
					     uint32_t symbols)
{
	auto coder = decoder->coder;
	size_t i;
	size_t read_index;
	size_t lost_read_blocks;
	uint32_t pos;
	uint32_t symbol_size = coder->symbol_size();

	/* is it outside our current window and to old to be a new seqno?
	 * then we have nothing which we have to save
	 */
	if (rbufmgr_outdated(&decoder->rbufmgr, sequence))
		return;

	/* get number of entries which will be lost after inserting */
	rbufmgr_shift_distance(&decoder->rbufmgr, sequence, &read_index,
			       &lost_read_blocks);

	/* nothing to save when nothing will get shifted outside the window */
	if (lost_read_blocks <= 0)
		return;

	/* everything will be lost which is outside the current window.
	 * the flush ("read till this seqno") must therefore be right
	 * at the beginning of the new window
	 */
	decoder->flush = sequence - symbols;

	/* find first entry with source which will be lost */
	for (i = 0; i < lost_read_blocks; i++) {
		pos = rbufmgr_peek(&decoder->rbufmgr);

		/* ignore undecoded symbols and "consume" this index */
		if (!coder->is_symbol_uncoded(pos) &&
		    !coder->check_symbol_status(pos)) {
			rbufmgr_read(&decoder->rbufmgr);
			decoder->stats.s[NCK_STATS_UNDECODED_SYMBOLS]++;
			continue;
		}

		decoder->has_source = 1;
		break;
	}

	/* inform consumer about new entries */
	if (_has_source(decoder))
		nck_trigger_call(&decoder->on_source_ready);

	/* lost_read_blocks is not accurate anymore!
	 * The consumer might have used some of the data already.
	 */
	rbufmgr_shift_distance(&decoder->rbufmgr, sequence, &read_index,
			       &lost_read_blocks);

	/* reset the queue
	 * if there is something inside it is the callers own fault
	 */
	decoder->queue_length = 0;
	decoder->queue_index = 0;

	/* copy entries which will be lost to queue */
	for (i = 0; i < lost_read_blocks; i++) {
		pos = rbufmgr_read(&decoder->rbufmgr);

		/* check whether it is a source symbol
		 */
		if (!coder->is_symbol_uncoded(pos) &&
		    !coder->check_symbol_status(pos))
			continue;

		/* don't overflow queue */
		assert(decoder->queue_length < coder->symbols());

		/* copy symbol to queue */
		auto src = &decoder->buffer[pos * symbol_size];
		auto dst = &decoder->queue[decoder->queue_length * symbol_size];
		memcpy(dst, src, symbol_size);
		decoder->queue_length += 1;
	}

	/* TODO this should not be possible because it first sets
	 * recoder->flush = sequence - symbols;
	 */
	// Now it is possible that we skipped a full rotation of the symbols.
	// As a result the flushing procedure above terminates at a point which
	// is still outside the new decoding window.
	// Here we first detect this case and handle it by placing the index
	// above the first symbol of the new window.
	if (coder->sequence_compare(rbufmgr_read_seqno(&decoder->rbufmgr) + symbols, sequence) < 0)
		decoder->flush = rbufmgr_read_seqno(&decoder->rbufmgr);

	// force a recheck of has_source
	decoder->has_source = 0;
}

/**
 * nck_sw_feedback_required - Check whether feedback is required
 * @decoder: decoder structure that will be used
 * @rank: rank in coder before packet was inserted
 * @sequence: received sequence number in packet
 *
 * Return: true when a feedback packet should be generated
 */
static bool nck_sw_feedback_required(struct nck_sw_dec *decoder, uint32_t rank,
				     uint32_t sequence)
{
	auto coder = decoder->coder;
	auto coder_rank = coder->rank();
	uint32_t symbols = coder->symbols();
	int32_t seqno_diff;

	/* don't send automatic feedback when it was disabled */
	if (decoder->feedback <= 0)
		return false;

	if (timerisset(&decoder->fb_timeout))
		return false;

	/* TODO document */
	if (coder_rank < rank)
		return true;

	/* TODO document */
	if (coder_rank == rank && rank != symbols)
		return true;

	/* TODO document */
	seqno_diff = coder->sequence_number() - sequence;
	if (coder_rank > rank && seqno_diff > 1)
		return true;

	return false;
}

EXPORT
int nck_sw_dec_put_coded(struct nck_sw_dec *decoder, struct sk_buff *packet)
{
	struct sw_coded_packet *sw_coded_packet;
	auto coder = decoder->coder;
	uint32_t symbols = coder->symbols();
	uint32_t pos;
	int read_payload_retcode;

	if (!pskb_may_pull(packet, sizeof(*sw_coded_packet)))
		return -1;

	sw_coded_packet = (struct sw_coded_packet *)packet->data;
	skb_pull(packet, sizeof(*sw_coded_packet));

	if (sw_coded_packet->packet_type != SW_PACKET_TYPE_CODED)
		return -1;

	/* should hold a least the sequence number + systematic flag */
	if (!pskb_may_pull(packet, decoder->header_size))
		return -1;

	decoder->stats.s[NCK_STATS_PUT_CODED]++;

	header_t header;
	decoder->coder->read_header(packet->data, header);

	if (!decoder->initialized) {
		// initialize decoder
		nck_sw_dec_set_sequence(decoder, header.sequence);
	}

	/* try to let the consumer get remaining symbols or copy source symbols
	 * to queue for later
	 */
	nck_sw_dec_put_coded_consume_old(decoder, header.sequence, symbols);

	auto rank = coder->rank();
	auto sequence = coder->sequence_number();

	// pad short packets with zeros before giving to the decoder
	skb_put_zeros(packet, coder->payload_size());
	read_payload_retcode = coder->read_payload(packet->data);
	switch (read_payload_retcode) {
	case READ_PAYLOAD_REDUNDANT:
		decoder->stats.s[NCK_STATS_PUT_CODED_REDUNDANT]++;
		break;
	case READ_PAYLOAD_INNOVATIVE:
		decoder->stats.s[NCK_STATS_PUT_CODED_INNOVATIVE]++;
		break;
	case READ_PAYLOAD_OUTDATED:
		decoder->stats.s[NCK_STATS_PUT_CODED_OUTDATED]++;
		break;
	case READ_PAYLOAD_CONFLICT:
		decoder->stats.s[NCK_STATS_PUT_CODED_CONFLICT]++;
		break;
	}
	rbufmgr_insert(&decoder->rbufmgr, header.sequence);

	decoder->feedback_packet_no = ntohs(sw_coded_packet->packet_no);
	if ((sw_coded_packet->flags & SW_CODED_PACKET_FEEDBACK_REQUESTED) ||
	    (sw_coded_packet->flags & SW_CODED_PACKET_FLUSH) ||
	    nck_sw_feedback_required(decoder, rank, sequence)) {
		// feedback requested
		decoder->has_feedback = 1;
		nck_trigger_call(&decoder->on_feedback_ready);
	}

	if (rbufmgr_empty(&decoder->rbufmgr)) {
		assert(!decoder->has_source);
	} else {
		pos = rbufmgr_peek(&decoder->rbufmgr);
		decoder->has_source = coder->is_symbol_uncoded(pos) || coder->check_symbol_status(pos);

		if (decoder->has_source &&
		    decoder->flush == rbufmgr_read_seqno(&decoder->rbufmgr)) {
			// if we have a source symbol then flush should be somewhere after that
			decoder->flush += 1;
		}
	}

	decoder->feedback_tx_attempts = 0;

	if (decoder->feedback) {
		// if feedback is enabled request more data from upstream
		if (timerisset(&decoder->fb_timeout)) {
			if (!nck_timer_pending(decoder->fb_timeout_handle))
				nck_timer_rearm(decoder->fb_timeout_handle, &decoder->fb_timeout);
		}
	} else if (decoder->flush != coder->sequence_number()) {
		// without feedback we should at some point just output what we have
		if (decoder->timeout_handle) {
			assert(timerisset(&decoder->timeout));
			nck_timer_rearm(decoder->timeout_handle, &decoder->timeout);
		}
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_sw_dec_get_source(struct nck_sw_dec *decoder, struct sk_buff *packet)
{
	auto coder = decoder->coder;
	uint32_t symbol_size = coder->symbol_size();
	uint8_t *symbol, *payload;

	decoder->stats.s[NCK_STATS_GET_SOURCE]++;

	if (decoder->queue_length != decoder->queue_index) {
		// here we get a packet from the queue
		symbol = &decoder->queue[decoder->queue_index*symbol_size];
		decoder->queue_index += 1;
	} else {
		// here the queue is empty
		// but we should have something in the decoder
		assert(decoder->has_source);

		uint32_t pos = rbufmgr_read(&decoder->rbufmgr);
		symbol = &decoder->buffer[pos * symbol_size];
		decoder->has_source = 0;

		move_to_next_source(decoder);
	}

	payload = (uint8_t *)skb_put(packet, symbol_size);
	memcpy(payload, symbol, symbol_size);

	return 0;
}

EXPORT
char *nck_sw_dec_describe_packet(void *dec, struct sk_buff *packet)
{
	struct nck_sw_dec *decoder = (struct nck_sw_dec*)dec;

	return nck_sw_common_describe_packet(packet, decoder->coder->symbols());
}

EXPORT
struct nck_stats *nck_sw_dec_get_stats(void *dec)
{
	struct nck_sw_dec *decoder = (struct nck_sw_dec*)dec;

	return &decoder->stats;
}

EXPORT
int nck_sw_dec_get_feedback(struct nck_sw_dec *decoder, struct sk_buff *packet)
{
	struct sw_feedback_packet *sw_feedback_packet;
	auto coder = decoder->coder;
	int bytes, index, symbols = decoder->coder->symbols();
	uint32_t seqno;
	uint32_t first_missing;

	decoder->stats.s[NCK_STATS_GET_FEEDBACK]++;

	bytes = DIV_ROUND_UP(symbols, 8);
	skb_reserve(packet, sizeof(*sw_feedback_packet));

	// select missing symbols
	uint8_t *payload = (uint8_t *)skb_put(packet, bytes);
	memset(payload, 0, bytes);

	/* mark all symbols which are not pivot.
	 *
	 * Only consider symbols between our last read sequence number and the decoder sequence number.
	 * Any older sequence numbers are either:
	 *  1) read
	 *  2) discarded (i.e. flushed)
	 *  3) never sent by the encoder (e.g. the stream started)
	 *
	 * and can therefore be ignored.
	 */
	first_missing = coder->sequence_number();
	assert(((uint32_t) coder->sequence_number() - rbufmgr_read_seqno(&decoder->rbufmgr)) <= (uint32_t)symbols);

	for (seqno = rbufmgr_read_seqno(&decoder->rbufmgr); seqno != coder->sequence_number(); seqno++) {
		index = seqno % symbols;
		if (!decoder->coder->is_symbol_pivot(index)) {
			packet->data[index/8] |= 1<<(index%8);
		}
		if (first_missing == coder->sequence_number()) {
			// if we have not yet found a missing symbol we might want to check
			if (!coder->is_symbol_uncoded(index) && !coder->check_symbol_status(index)) {
				first_missing = seqno;
			}
		}
	}

	nck_trace(decoder, "pktno=%u fbno=%u seq=%u miss=%u",
			decoder->feedback_packet_no, decoder->feedback_no,
			decoder->coder->sequence_number(), first_missing);

	// write sequence number and packet number
	sw_feedback_packet = (struct sw_feedback_packet *)skb_push(packet, sizeof(*sw_feedback_packet));
	memset(sw_feedback_packet, 0, sizeof(*sw_feedback_packet));
	sw_feedback_packet->packet_type = SW_PACKET_TYPE_FEEDBACK;
	sw_feedback_packet->order = decoder->order;
	sw_feedback_packet->packet_no = htons(decoder->feedback_packet_no);
	sw_feedback_packet->sequence = htonl(decoder->coder->sequence_number());
	sw_feedback_packet->feedback_no = htons(decoder->feedback_no++);
	sw_feedback_packet->first_missing = htonl(first_missing);

	// stop sending feedback
	decoder->has_feedback = 0;

	if (timerisset(&decoder->fb_timeout)) {
		if (!_complete(decoder))
			nck_timer_rearm(decoder->fb_timeout_handle, &decoder->fb_timeout);
		else
			nck_timer_cancel(decoder->fb_timeout_handle);
	}

	return 0;
}

EXPORT
int nck_sw_dec_has_feedback(struct nck_sw_dec *decoder)
{
	return decoder->has_feedback;
}

