#include <cstdint>
#include <cstdbool>
#include <cstdlib>

#include <cerrno>
#include <cstring>

#include <sys/time.h>
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

#include <kodo_sliding_window/sliding_window_encoder.hpp>

#include <boost/circular_buffer.hpp>

typedef kodo_sliding_window::sliding_window_encoder::factory factory_t;
typedef factory_t::pointer coder_t;

struct kodo_header {
	uint32_t seqno;
	uint8_t systematic_flag;
} __packed;

struct nck_interflow_sw_enc {
	nck_interflow_sw_enc(coder_t coder, int ord) :
		coder(coder), source_size(coder->symbol_size()),
		coded_size(sizeof(struct interflow_sw_coded_packet) + coder->payload_size()),
		feedback_size(sizeof(struct interflow_sw_feedback_packet) + DIV_ROUND_UP(coder->symbols(), 8)),
		initialized(0), window_size(coder->symbols()),
		cfg_systematic_phase(coder->symbols()), cfg_coded_phase(1),
		source_symbols(0), index(0), order(ord), first_missing(0),
		feedback_only_on_repair(0), coded_retrans(0),
		feedback_period(1), packet_count(0), systematic_time(coder->symbols()), coded_time(coder->symbols()),
		max_tx_attempts(UINT8_MAX), tx_attempts(coder->symbols()), flush_attempts(0), flush_next(0),
		packet_memory(0), coded_packets(1), timeout(), timeout_handle(), on_coded_ready(),
		buffer(coder->block_size()), node_id(0), n_nodes(0)
	{
		nck_trigger_init(&on_coded_ready);
		rate_control_dual_init(&rc, cfg_systematic_phase, cfg_coded_phase);

		memset(&stats, 0, sizeof(stats));
	}

	coder_t coder;

	size_t source_size, coded_size, feedback_size;
	size_t header_size;

	int initialized;

	int window_size;
	int forward_code_window;

	// rate control
	int cfg_systematic_phase, cfg_coded_phase;
	struct rate_control rc;

	// total number of source packets to send
	int source_symbols;
	uint32_t index;
	uint8_t order;
	uint32_t first_missing;

	// feedback mechanism
	int feedback_only_on_repair;
	int coded_retrans; // retransmit only coded packets
	int feedback_period;

	uint16_t packet_count;
	std::vector<uint16_t> systematic_time;
	std::vector<uint16_t> coded_time;

	uint8_t max_tx_attempts;
	std::vector<uint8_t> tx_attempts;
	uint8_t flush_attempts;
	uint8_t flush_next;

	// keep track when we sent coded packets
	int packet_memory;
	boost::circular_buffer<uint16_t> coded_packets;

	struct nck_stats stats;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	struct nck_trigger on_coded_ready;

	std::vector<uint8_t> buffer;

	// Use a unique identifier for each node to enable interflow coding
	uint32_t node_id;
	uint32_t n_nodes;

};

char *nck_interflow_sw_enc_debug(void *encoder);
char *nck_interflow_sw_enc_describe_packet(void *encoder, struct sk_buff *packet);
struct nck_stats *nck_interflow_sw_enc_get_stats(void *encoder);

NCK_ENCODER_IMPL(nck_interflow_sw, nck_interflow_sw_enc_debug, nck_interflow_sw_enc_describe_packet, nck_interflow_sw_enc_get_stats)

EXPORT
void nck_interflow_sw_enc_set_feedback_only_on_repair(struct nck_interflow_sw_enc *encoder, uint32_t feedback_only_on_repair)
{
	encoder->feedback_only_on_repair = feedback_only_on_repair;
}

void nck_interflow_sw_enc_set_node_id(struct nck_interflow_sw_enc *encoder, uint32_t node_id)
{
	encoder->node_id = node_id;
}

void nck_interflow_sw_enc_set_n_nodes(struct nck_interflow_sw_enc *encoder, uint32_t n_nodes)
{
	encoder->n_nodes = n_nodes;
}

EXPORT
void nck_interflow_sw_enc_set_sequence(struct nck_interflow_sw_enc *encoder, uint32_t sequence)
{
	encoder->index = sequence;
	encoder->first_missing = sequence;
}

EXPORT
void nck_interflow_sw_enc_set_forward_code_window(struct nck_interflow_sw_enc *encoder, uint32_t forward_code_window)
{
	encoder->forward_code_window = forward_code_window;
}

EXPORT
void nck_interflow_sw_enc_set_coded_retransmissions(struct nck_interflow_sw_enc *encoder, int coded_retrans)
{
	encoder->coded_retrans = coded_retrans;
}

EXPORT
void nck_interflow_sw_enc_set_redundancy(struct nck_interflow_sw_enc *encoder, int32_t redundancy)
{
	if (encoder->rc.algo != RATE_CONTROL_CREDIT) {
		/* reject setting when already initialized */
		if (encoder->initialized)
			return;

		/* otherwise we have to initialized it again */
		auto coder = encoder->coder;
		rate_control_credit_init(&encoder->rc, coder->symbols(), redundancy);
	}

	/* encoder is not allowed to send less symbols than it was given
	 * as source. The redudancy must therefore at least be 0.
	 */
	rate_control_credit_change(&encoder->rc,  max_t(int32_t, 0, redundancy));
}

EXPORT
void nck_interflow_sw_enc_set_systematic_phase(struct nck_interflow_sw_enc *encoder, uint32_t phase_length)
{
	if (encoder->rc.algo != RATE_CONTROL_DUAL) {
		/* reject setting when already initialized */
		if (encoder->initialized)
			return;

		/* otherwise we have to initialized it again */
		auto coder = encoder->coder;
		rate_control_dual_init(&encoder->rc, encoder->cfg_systematic_phase, encoder->cfg_coded_phase);
	}

	if (phase_length == 0) {
		phase_length = encoder->coder->symbols();
	}

	encoder->cfg_systematic_phase = phase_length;
	rate_control_dual_change(&encoder->rc, encoder->cfg_systematic_phase, encoder->cfg_coded_phase);
}

EXPORT
void nck_interflow_sw_enc_set_coded_phase(struct nck_interflow_sw_enc *encoder, uint32_t phase_length)
{
	if (encoder->rc.algo != RATE_CONTROL_DUAL) {
		/* reject setting when already initialized */
		if (encoder->initialized)
			return;

		/* otherwise we have to initialized it again */
		auto coder = encoder->coder;
		rate_control_dual_init(&encoder->rc, encoder->cfg_systematic_phase, encoder->cfg_coded_phase);
	}

	encoder->cfg_coded_phase = phase_length;
	rate_control_dual_change(&encoder->rc, encoder->cfg_systematic_phase, encoder->cfg_coded_phase);
}

EXPORT
void nck_interflow_sw_enc_set_feedback_period(struct nck_interflow_sw_enc *encoder, uint32_t period)
{
	encoder->feedback_period = period;

	/* it is invalid to set feedback to 0 and then expect to send a symbol
	 * multiple times. This would never clean up transmitted packets and
	 * therefore never free the encoder for new source symbols
	 */
	if (encoder->max_tx_attempts > 1 && encoder->feedback_period == 0)
		encoder->max_tx_attempts = 1;
}

EXPORT
void nck_interflow_sw_enc_set_memory(struct nck_interflow_sw_enc *encoder, uint32_t memory_size)
{
	encoder->packet_memory = memory_size;

	if (memory_size == 0) {
		// zero size is not supported
		encoder->coded_packets.resize(1);
	} else {
		encoder->coded_packets.resize(memory_size);
	}
}

EXPORT
void nck_interflow_sw_enc_set_tx_attempts(struct nck_interflow_sw_enc *encoder, uint8_t tx_attempts)
{
	encoder->max_tx_attempts = tx_attempts;

	/* it is invalid to set feedback to 0 and then expect to send a symbol
	 * multiple times. This would never clean up transmitted packets and
	 * therefore never free the encoder for new source symbols
	 */
	if (encoder->max_tx_attempts > 1 && encoder->feedback_period == 0)
		encoder->feedback_period = 1;
}

static void encoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		struct nck_interflow_sw_enc *encoder = (struct nck_interflow_sw_enc *)context;
		encoder->stats.s[NCK_STATS_TIMER_FLUSH]++;
		// the timeout flush should happen only if we have nothing to send...
		// but if it does anyway we just do nothing
		if (!_has_coded(encoder)) {
			_flush_coded(encoder);
		}
	}
}

EXPORT
struct nck_interflow_sw_enc *nck_interflow_sw_enc(uint32_t symbols, uint32_t symbol_size, struct nck_timer *timer,
		const struct timeval *timeout)
{
	uint8_t ord = 0;
	while (symbols > (1U<<ord))
		++ord;

	if (symbols != (1U<<ord)) {
		// TODO: raise a proper error
		fprintf(stderr, "Number of symbols must be a power of 2 %u %u %u\n", symbols, ord, 1U<<ord);
		return NULL;
	}

	factory_t factory(fifi::api::field::binary8, symbols, symbol_size);

	struct nck_interflow_sw_enc *result = new struct nck_interflow_sw_enc(factory.build(), ord);
	result->header_size = factory.header_size();

	if (timeout && timerisset(timeout)) {
		assert(timer != NULL);
		result->timeout = *timeout;
		result->timeout_handle = nck_timer_add(timer, NULL, result, encoder_timeout_flush);
	}
	return result;
}

EXPORT
void nck_interflow_sw_enc_free(struct nck_interflow_sw_enc *encoder)
{
	if (encoder->timeout_handle) {
		nck_timer_cancel(encoder->timeout_handle);
		nck_timer_free(encoder->timeout_handle);
	}
	delete encoder;
}

EXPORT
int nck_interflow_sw_enc_has_coded(struct nck_interflow_sw_enc *encoder)
{
	if (encoder->source_symbols > 0)
		return true;

	if (rate_control_next_repair(&encoder->rc, encoder->source_symbols))
		return true;

	return false;
}

EXPORT
int nck_interflow_sw_enc_full(struct nck_interflow_sw_enc *encoder)
{
	/* avoid new symbols when coded phase has not yet ended */
	if (!rate_control_insert_allowed(&encoder->rc, encoder->source_symbols))
		return true;

	/* the first space is not free */
	if (encoder->tx_attempts[encoder->index] != 0)
		return true;

	// if we have no feedback then the encoder is never blocked
	if (encoder->feedback_period == 0 && encoder->feedback_only_on_repair == 0) {
		return false;
	}

	// the decoder indicated that we may not shift the window
	// we honor this only if max_tx_attempts is the maximum value
	// we interpret this to mean it should be 100% reliable
	if (encoder->max_tx_attempts == UINT8_MAX && encoder->coder->sequence_number() - encoder->first_missing == encoder->coder->symbols()) {
		return true;
	}

	return false;
}

EXPORT
int nck_interflow_sw_enc_complete(struct nck_interflow_sw_enc *encoder)
{
	if (_has_coded(encoder)) {
		// if we have more to send we are not complete
		return 0;
	}

	if (encoder->timeout_handle && nck_timer_pending(encoder->timeout_handle)) {
		// if we have a timeout running we are not complete
		return 0;
	}

	return 1;
}

static void nck_interflow_sw_enc_free_symbols(struct nck_interflow_sw_enc *encoder)
{
	auto coder = encoder->coder;
	uint32_t encoder_sequence;
	uint32_t symbols;
	uint32_t rank;
	uint32_t i;
	uint32_t s;
	uint32_t rs;

	symbols = coder->symbols();
	rank = coder->rank();
	encoder_sequence = coder->sequence_number();

	for (i = 0; i < rank; i++) {
		s = encoder_sequence - rank + 1 + i;
		rs = s % symbols;

		encoder->coder->disable_symbol(rs);
		encoder->tx_attempts[rs] = 0;
		if (encoder->coder->is_systematic(rs)) {
			// we have to decrement the source_symbols counter
			assert(encoder->source_symbols > 0);
			encoder->coder->disable_systematic_symbol(rs);
			encoder->source_symbols -= 1;
		}
	}
}

EXPORT
void nck_interflow_sw_enc_flush_coded(struct nck_interflow_sw_enc *encoder)
{
	auto coder = encoder->coder;

	/* only flush data when rank > 0 (some symbols were added to coder)
	 * and when enabled_symbols > 0 (some packets were not yet acknowledged)
	 */
	if (coder->rank() <= 0 || coder->enabled_symbols() <= 0)
		return;

	/* avoid that flush attempts are done indefinitely */
	if (encoder->flush_attempts <= 0) {
		/* free symbols which will not be transmitted anymore */
		nck_interflow_sw_enc_free_symbols(encoder);
		return;
	}

	if (encoder->max_tx_attempts != UINT8_MAX) {
		encoder->flush_attempts--;
	}
	encoder->flush_next = 1;

	if (!rate_control_has_repair(&encoder->rc)) {
		/* fallback to sending systematic symbols when coded was
		 * set to 0
		 */
		auto symbols = coder->symbols();
		// transmit a systematic symbol which has attempts left

		// we send the last symbol to force the encoder into sending
		// the newest sequence number
		for (uint32_t i = 1; i <= symbols; ++i) {
			uint32_t s = (encoder->index - i + symbols) % symbols;
			if (encoder->tx_attempts[s] <= 0)
				continue;

			/* retransmit the last symbol with tx_attemps > 0 */
			coder->enable_symbol(s);
			if (!coder->is_systematic(s)) {
				coder->enable_systematic_symbol(s);
				encoder->source_symbols += 1;
			}
			rate_control_reset_source_min(&encoder->rc);
			break;
		}

		/* TODO check if something should be done when
		 * feedback != 0 and no symbol has a tx_attemps > 0
		 */
	} else if (!rate_control_next_repair(&encoder->rc, encoder->source_symbols)) {
		// end the systematic phase
		rate_control_reset_repair(&encoder->rc);
	}

	assert(!rate_control_has_repair(&encoder->rc) || _has_coded(encoder));

	if (_has_coded(encoder))
		nck_trigger_call(&encoder->on_coded_ready);
}

EXPORT
int nck_interflow_sw_enc_put_source(struct nck_interflow_sw_enc *encoder, struct sk_buff *packet)
{
	auto coder = encoder->coder;
	uint32_t symbols = coder->symbols();
	uint32_t symbol_size = coder->symbol_size();
	uint32_t index = encoder->index;

	encoder->initialized = 1;

	// get pointer to memory location
	uint8_t *symbol = &encoder->buffer[index * symbol_size];

	encoder->stats.s[NCK_STATS_PUT_SOURCE]++;

	if (encoder->coder->is_systematic(index)) {
		// adding a new symbol would overwrite a old symbol
		encoder->coder->disable_systematic_symbol(index);
		encoder->source_symbols -= 1;
	}

	// copy packet into that memory
	memcpy(symbol, packet->data, packet->len);
	// fill the rest with zeros
	memset(symbol+packet->len, 0, symbol_size - packet->len);

	// close the window
	// the symbol should not be activated as systematic
	// IMPORTANT: if we want to allow shifting out a symbol marked for systematic coding
	// then we also need to decrement the encoder->source_symbols counter!
	assert(!coder->is_systematic(index));
	coder->disable_symbol((index-encoder->forward_code_window+symbols)%symbols);

	// pass the memory to the kodo encoder
	coder->set_const_symbol(index, storage::storage(symbol, symbol_size));
	encoder->tx_attempts[index] = encoder->max_tx_attempts;

	/* allow flushing again */
	encoder->flush_attempts = max_t(uint8_t, encoder->max_tx_attempts, 1) - 1;

	// move the index to the next location
	encoder->index = (index + 1) % symbols;

	// we can send one more packet
	encoder->source_symbols += 1;
	rate_control_insert(&encoder->rc, false);

	if (encoder->timeout_handle) {
		// we definitelly have something to send now, so we cancel the timeout
		nck_timer_cancel(encoder->timeout_handle);
	}

	nck_trigger_call(&encoder->on_coded_ready);
	return 0;
}

EXPORT
int nck_interflow_sw_enc_get_coded(struct nck_interflow_sw_enc *encoder, struct sk_buff *packet)
{
	struct kodo_header *kodo_header;
	struct interflow_sw_coded_packet *interflow_sw_coded_packet;
	int repair;

	assert(_has_coded(encoder));

	auto coder = encoder->coder;

	encoder->stats.s[NCK_STATS_GET_CODED]++;

	skb_reserve(packet, sizeof(*interflow_sw_coded_packet));

	encoder->packet_count += 1;

	repair = rate_control_step(&encoder->rc, encoder->source_symbols);
	if (!repair) {
		coder->set_systematic_on();
		// one less source symbol to send
		assert(coder->in_systematic_phase());

		encoder->source_symbols -= 1;

		int index = coder->next_systematic_symbol();
		assert(encoder->tx_attempts[index] > 0); // catch integer overflows

		encoder->systematic_time[index] = encoder->packet_count;
		encoder->coded_time[index] = encoder->packet_count;
		if (encoder->max_tx_attempts != UINT8_MAX) {
			encoder->tx_attempts[index]--;
		}
		if (encoder->tx_attempts[index] == 0)
			encoder->stats.s[NCK_STATS_GET_CODED_TX_ATTEMPTS_ZERO]++;
		encoder->stats.s[NCK_STATS_GET_CODED_SYSTEMATIC]++;
	} else {
		coder->set_systematic_off();
		assert(!coder->in_systematic_phase());

		for (uint32_t i = 0; i < coder->symbols(); ++i) {
			if (coder->is_symbol_enabled(i)) {
				encoder->coded_time[i] = encoder->packet_count;
			}
		}

		encoder->coded_packets.push_back(encoder->packet_count);
		encoder->stats.s[NCK_STATS_GET_CODED_REPAIR]++;
	}

	size_t payload_size = coder->payload_size();
	uint8_t *payload = (uint8_t *)skb_put(packet, payload_size);
	size_t real_size = coder->write_payload(payload);

	assert(real_size <= payload_size);
	skb_trim(packet, payload_size - real_size);
	skb_trim_zeros(packet);

	/* do not trim off the minimal header as well. */
	if (packet->len < encoder->header_size) {
		skb_put(packet, encoder->header_size - packet->len);
	}

	int flags = 0;

	if (encoder->feedback_only_on_repair) {
		if (repair)
			flags |= INTERFLOW_SW_CODED_PACKET_FEEDBACK_REQUESTED;
	} else {
		if (!_has_coded(encoder)) {
			// we request feedback for the last packet
			flags |= INTERFLOW_SW_CODED_PACKET_FEEDBACK_REQUESTED;
		} else if (encoder->tx_attempts[encoder->index]) {
			// we request feedback if the coding window gets too full
			flags |= INTERFLOW_SW_CODED_PACKET_FEEDBACK_REQUESTED;
		} else if (encoder->feedback_period > 0 && encoder->packet_count % encoder->feedback_period == 0) {
			// we request feedback if the period is reached
			flags |= INTERFLOW_SW_CODED_PACKET_FEEDBACK_REQUESTED;
		}
	}

	/* never send feedback when it was disabled */
	if (encoder->feedback_period <= 0)
		flags &= ~INTERFLOW_SW_CODED_PACKET_FEEDBACK_REQUESTED;

	/* flush this packet */
	if (encoder->flush_next) {
		flags |= INTERFLOW_SW_CODED_PACKET_FLUSH;
		encoder->flush_next = 0;
	}

	interflow_sw_coded_packet = (struct interflow_sw_coded_packet *)skb_push(packet, sizeof(*interflow_sw_coded_packet));
	memset(interflow_sw_coded_packet, 0, sizeof(*interflow_sw_coded_packet));
	interflow_sw_coded_packet->packet_type = INTERFLOW_SW_PACKET_TYPE_CODED;
	interflow_sw_coded_packet->order = encoder->order;
	interflow_sw_coded_packet->flags = flags;

	kodo_header = (struct kodo_header *) (interflow_sw_coded_packet + 1);
	uint32_t seqno = ntohl(kodo_header->seqno);
	uint32_t new_seqno = htonl((seqno-1) * encoder->n_nodes + encoder->node_id + 1);
	memcpy (kodo_header, &new_seqno, sizeof(new_seqno));
	uint16_t new_packetno = (uint16_t)(ntohl(new_seqno));
	interflow_sw_coded_packet->packet_no = htons(new_packetno);
	if (encoder->timeout_handle && !_has_coded(encoder)) {
		// We have nothing more to send, so we register a timeout.
		// The timeout should be reset if either a new source packet
		// is added or feedback arrives.
		nck_timer_rearm(encoder->timeout_handle, &encoder->timeout);
	}

	return 0;
}

EXPORT
char *nck_interflow_sw_enc_debug(void *enc)
{
	struct nck_interflow_sw_enc *encoder = (struct nck_interflow_sw_enc*)enc;
	static char debug[40000];
	int len = sizeof(debug), pos = 0;

	debug[0] = 0;

	switch (encoder->rc.algo) {
	case RATE_CONTROL_DUAL:
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"systematic_phase\":%d,", encoder->rc.dual.source_phase);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"coded_phase\":%d,", encoder->rc.dual.repair_phase);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"systematic_counter\":%d,", encoder->rc.dual.source_counter);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"coded_counter\":%d,", encoder->rc.dual.repair_counter);
		break;
	case RATE_CONTROL_CREDIT:
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"redundancy\":%d,", encoder->rc.credit.redundancy);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"credit_counter\":%d,", encoder->rc.credit.counter);
		break;
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"source_symbols\":%d,", encoder->source_symbols);

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"coded_packets\":\"");
	for (auto it = encoder->coded_packets.begin(); it != encoder->coded_packets.end(); ++it) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%d ", *it);
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\",\"systematic_time\":\"");
	for (auto it = encoder->systematic_time.begin(); it != encoder->systematic_time.end(); ++it) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%d ", *it);
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\",\"coded_time\":\"");
	for (auto it = encoder->coded_time.begin(); it != encoder->coded_time.end(); ++it) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%d ", *it);
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\",\"next_systematic\":%d", encoder->coder->next_systematic_symbol());
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), ",\"systematic\":\"");
	for (unsigned i = 0; i < encoder->coder->symbols(); ++i) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%d ", encoder->coder->is_systematic(i));
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\",\"tx_attempts\":\"");
	for (auto it = encoder->tx_attempts.begin(); it != encoder->tx_attempts.end(); ++it) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%d ", *it);
	}
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"");

	debug[sizeof(debug) - 1] = 0;

	return debug;
}

EXPORT
char *nck_interflow_sw_enc_describe_packet(void *enc, struct sk_buff *packet)
{
	struct nck_interflow_sw_enc *encoder = (struct nck_interflow_sw_enc*)enc;

	return nck_interflow_sw_common_describe_packet(packet, encoder->coder->symbols());
}

EXPORT
struct nck_stats *nck_interflow_sw_enc_get_stats(void *enc)
{
	struct nck_interflow_sw_enc *encoder = (struct nck_interflow_sw_enc*)enc;

	return &encoder->stats;
}

/**
 * nck_sw_feedback_seqno_valid() - check if received feedback seqno is valid
 * @sequence: Received sequence number in feedback
 * @encoder_sequence: Current sequence number in coder
 * @encoder_rank: current rank of the encoder
 *
 * Return: true when the received sequence number is valid for current coder
 *  window
 */
static bool nck_interflow_sw_feedback_seqno_valid(uint32_t sequence,
					uint32_t encoder_sequence,
					uint32_t encoder_rank)
{
	int32_t seqno_dist;
	uint32_t older_than_encoder_seqno;

	seqno_dist = sequence - encoder_sequence;

	/* feedback is newer than our own sequence number -> error */
	if (seqno_dist > 0)
		return false;

	/* feedback is older than our current window size */
	older_than_encoder_seqno = -seqno_dist;
	if (older_than_encoder_seqno >= encoder_rank)
		return false;

	return true;
}

EXPORT
int nck_interflow_sw_enc_put_feedback(struct nck_interflow_sw_enc *encoder, struct sk_buff *packet)
{
	auto coder = encoder->coder;
	int symbols = encoder->coder->symbols();
	int i;
	uint32_t s;
	struct interflow_sw_feedback_packet *interflow_sw_feedback_packet;
	uint16_t feedback_packet_no;
	uint32_t sequence;
	uint32_t first_missing;
	uint32_t encoder_sequence = encoder->coder->sequence_number();
	uint32_t rank;

	// check length
	if (!pskb_may_pull(packet, encoder->feedback_size))
		return -1;

	encoder->stats.s[NCK_STATS_PUT_FEEDBACK]++;

	interflow_sw_feedback_packet = (struct interflow_sw_feedback_packet *)packet->data;
	skb_pull(packet, sizeof(*interflow_sw_feedback_packet));

	if (interflow_sw_feedback_packet->packet_type != INTERFLOW_SW_PACKET_TYPE_FEEDBACK)
		return -1;

	feedback_packet_no = ntohs(interflow_sw_feedback_packet->packet_no);
	sequence = ntohl(interflow_sw_feedback_packet->sequence);
	first_missing = ntohl(interflow_sw_feedback_packet->first_missing);

	nck_trace(encoder, "pktno=%u fbno=%u seq=%u miss=%u",
			feedback_packet_no, ntohs(interflow_sw_feedback_packet->feedback_no),
			sequence, first_missing);

	/*
	 * Not all bits from the feedback are useful.
	 * But it surprisingly easy to enumerate all useful bits.
	 *
	 * [-------|--------|-------]  Bitmask
	 *         |        |
	 *         +------->+  Usable bits
	 *         |        |
	 *      Encoder    Feedback
	 *      Sequence   Sequence
	 *         =
	 *      Encoder Sequence - Symbols
	 *
	 * Sometimes the encoder does not have all symbols, so the earliest symbol
	 * is actually `encoder_sequence - rank`
	 */

	int16_t total_in_flight = encoder->packet_count - feedback_packet_no;
	if (total_in_flight < 0) {
		// in this case the feedback must be REALLY outdated, so we ignore it
		return 0;
	}

	if ((int)(first_missing - encoder->first_missing) > 0) {
		// we should only move the "first missing" forward, never back
		encoder->first_missing = first_missing;
	}

	// find first coded packet that is in flight
	// we use this later as start for iterations
	// TODO: we could also delete all skipped packets, if we assume no feedback reordering
	auto in_flight = encoder->coded_packets.begin();
	while (in_flight != encoder->coded_packets.end() && (int)(*in_flight - feedback_packet_no) < 0) {
		++in_flight;
	}

	int use_index, resend, losses = 0, resend_counter = 0;
	std::vector<uint8_t> used(encoder->coded_packets.size());

	// Since the encoder never misses any packets the rank is basically the number
	// of consecutive symbols available to the encoder.
	// We use this number to make sure that we do not iterate beyond the symbols that
	// are actually available to the encoder.
	rank = encoder->coder->rank();

	// TODO: maybe this check should be earlier?
	if (!nck_interflow_sw_feedback_seqno_valid(sequence, encoder_sequence, rank))
		return -1;

	for (s = encoder_sequence - rank; s != sequence; ++s) {
		/* TODO why is it valid that the data is parsed relative to the state of the encoder (its rank)?
		 * why isn't here any size check to see that i actually fits in packet->data?
		 */
		i = s%symbols;

		if ((packet->data[i/8] & (1 << (i%8))) == 0) {
			// packet was marked as acknowledged
			encoder->coder->disable_symbol(i);
			encoder->tx_attempts[i] = 0;
			if (encoder->coder->is_systematic(i)) {
				// we have to decrement the source_symbols counter
				assert(encoder->source_symbols > 0);
				encoder->coder->disable_systematic_symbol(i);
				encoder->source_symbols -= 1;
			} else if ((int16_t)(encoder->systematic_time[i] - encoder->packet_count) > 0) {
				// This case can happen for coded-only retransmissions.
				// We faked the systematic time with a future value.
				// And in this case we should decrement the retransmission counter.
				assert(encoder->rc.algo == RATE_CONTROL_DUAL);
				assert(encoder->rc.dual.repair_counter > 0);
				encoder->rc.dual.repair_counter -= 1;
			}
			continue;
		}

		// packet is missing
		losses += 1;

		// check if a retransmission is already planned for the future
		if ((int16_t)(encoder->systematic_time[i] - feedback_packet_no) > 0) {
			// we can ignore this feedback
			continue;
		}

		if (encoder->packet_memory && (int16_t)(encoder->coded_time[i] - feedback_packet_no) >= 0) {
			// if the timestamp is between the last systematic and last coded packet
			// we might want to do some checks to make sure we need to retransmit

			// so the following idea might not be completely true:
			// we search for the first unused coded packet that is in flight and can fix this error
			// if we find such a packet, we mark it as used
			// else we trigger a retransmission
			use_index = 0;
			resend = 1;
			for (auto it = in_flight; it != encoder->coded_packets.end(); ++it, ++use_index) {
				if ((int)(encoder->coded_time[i] - *it) < 0) {
					// we passed the last relevant coded packet
					break;
				}
				if ((int)(*it - encoder->coded_time[i]) < 0) {
					// we have not yet reached the first relevant coded packet
					continue;
				}

				if (!used[use_index]) {
					resend = 0;
					used[use_index] = 1;
					break;
				}
			}

			// why this might fail:
			// it only estimates how the decoder could use the packets
			// if there is reordering or losses the matrix might look completely different

			if (!resend) {
				continue;
			}
		}

		/* ignore packets with empty retry counter */
		if (encoder->tx_attempts[i] <= 0)
			continue;

		// this enables the symbol for retransmission
		encoder->coder->enable_symbol(i);
		if (!encoder->coder->is_systematic(i)) {
			resend_counter += 1;

			if (!encoder->coded_retrans || encoder->rc.algo != RATE_CONTROL_DUAL) {
				// do systematic retransmission
				encoder->stats.s[NCK_STATS_GET_CODED_RETRY]++;
				encoder->coder->enable_systematic_symbol(i);
				encoder->source_symbols += 1;
				rate_control_insert(&encoder->rc, true);
				encoder->flush_attempts = max_t(uint8_t, encoder->max_tx_attempts, 1) - 1;
			} else {
				// If we do only coded retransmissions, the systematic time will never be updated.
				// However, remembering the last retransmission time is crucial for the feedback process.
				// If we don't remember the retransmission, the next feedback packet will trigger additional retransmissions.
				// To counter this we "fake" the systematic_time with the time when these retransmissions should happen.
				encoder->systematic_time[i] = encoder->packet_count + resend_counter;
			}
		}
	}

	if (encoder->coded_retrans && encoder->rc.algo == RATE_CONTROL_DUAL && resend_counter > 0) {
		// immediatelly start the repair phase with as many coded retransmissions
		if (encoder->rc.dual.repair_counter == 0) {
			rate_control_dual_reset(&encoder->rc, 1, resend_counter);
		} else {
			encoder->rc.dual.repair_counter += resend_counter;
		}
	}

	if (coder->enabled_symbols() == 0 &&
			rate_control_next_repair(&encoder->rc, encoder->source_symbols)) {
		// not sure about this assert
		// encoder->source_symbols must be 0 in this case but it is not
		// clear to me whether it will automatically be 0 or if we should set it
		//assert(encoder->source_symbols == 0);

		// end coded phase since we have no need to code anything
		rate_control_reset_source(&encoder->rc);
	}

	if (encoder->timeout_handle) {
		if (_has_coded(encoder) && losses == 0) {
			// We cancel the timer here. Either everything was successful and
			// we do not need a retransmission, or a resend is planned already.
			nck_timer_cancel(encoder->timeout_handle);
		} else if (!nck_timer_pending(encoder->timeout_handle)) {
			nck_timer_rearm(encoder->timeout_handle, &encoder->timeout);
		}
	}

	if (_has_coded(encoder))
		nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}
