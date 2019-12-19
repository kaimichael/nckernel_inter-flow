#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/pace.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"


struct nck_pace_dec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	uint32_t generation;
	uint32_t block_size;
	uint32_t symbols;
	uint32_t rank;
	uint32_t prev_rank;

	size_t source_size, coded_size, feedback_size;

	uint32_t index;

	struct nck_timer *timer; 				// should be pointer?
	struct timeval dec_fb_timeout;

	struct nck_timer_entry *dec_fb_timeout_handle;
	struct timeval dec_flush_timeout;

	struct nck_timer_entry *dec_flush_timeout_handle;
	struct nck_trigger on_source_ready;

	uint32_t feedback_generation;
	uint32_t feedback_rank;

	struct nck_trigger on_feedback_ready;
	uint32_t last_sender_rank;

	int flush;

	uint8_t *buffer;

	uint8_t *queue;
	int queue_index;
	int queue_length;
};

NCK_DECODER_IMPL(nck_pace, nck_pace_dec_debug, NULL, NULL)

static void dec_start_next_generation(struct nck_pace_dec *decoder, uint32_t generation)
{
	decoder->generation = generation;
	decoder->rank = 0;
	decoder->flush = 0;
	decoder->index = 0;
	decoder->last_sender_rank = 0;

	decoder->feedback_rank = 0;
	decoder->feedback_generation=0;

	if (decoder->coder){
		krlnc_delete_decoder(decoder->coder);
	}
	decoder->coder = kodo_build_decoder(decoder->factory);
	krlnc_decoder_set_status_updater_on(decoder->coder);
	memset(decoder->buffer, 0, decoder->block_size);
	krlnc_decoder_set_mutable_symbols(decoder->coder, decoder->buffer, decoder->block_size);
}

static void decoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success){
	UNUSED(entry);

	if (success) {
		struct nck_pace_dec *decoder = (struct nck_pace_dec *) context;
		UNUSED(decoder);

		//todo: is this the right thing to do? How do we recreate the encoder when put_src is called later?
		// nck_pace_dec_free(decoder);
	}
}

static void decoder_send_feedback(struct nck_timer_entry *entry, void *context, int success){
	UNUSED(entry);

	if (success) {
		struct nck_pace_dec *decoder = (struct nck_pace_dec *)context;

		decoder->feedback_rank = decoder->rank;
		decoder->feedback_generation = decoder->generation;
		nck_trigger_call(&decoder->on_feedback_ready);
	}
}

EXPORT
char *nck_pace_dec_debug(void *dec)
{
	static char debug[1024];
	struct nck_pace_dec *decoder = dec;

	snprintf(debug, sizeof(debug) - 1, "generation %d, index %d, flush %d, rank %d, complete %d, has source %d",
		 decoder->generation, decoder->index, decoder->flush,
		 krlnc_decoder_rank(decoder->coder),
		 _complete(decoder), _has_source(decoder)
	);
	debug[sizeof(debug) - 1] = 0;

	return debug;
}

EXPORT
struct nck_pace_dec *nck_pace_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer)
{
	struct nck_pace_dec *result;
	uint32_t block_size;
	krlnc_decoder_t dec;

	// create decoder to get some parameter values
	dec = kodo_build_decoder(factory);
	block_size = krlnc_decoder_block_size(dec);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->buffer = malloc(block_size);

	result->symbols = krlnc_decoder_factory_symbols(factory);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(dec) + 6;
	result->feedback_size = 6;

	result->factory = factory;
	result->block_size = block_size;

	result->queue = malloc(result->symbols * result->source_size);
	result->queue_index = 0;
	result->queue_length = 0;

	dec_start_next_generation(result, 1);

	result->timer = timer;

	result->dec_fb_timeout_handle = nck_timer_add(timer, NULL, result, decoder_send_feedback);
	result->dec_flush_timeout_handle = nck_timer_add(timer, NULL, result, decoder_timeout_flush);

	struct timeval timeout_fb, timeout_flush;

	nck_pace_set_dec_fb_timeout(result, double_to_tv(0.050, &timeout_fb));
	nck_pace_set_dec_flush_timeout(result, double_to_tv(60.0, &timeout_flush));

	krlnc_delete_decoder(dec);

	return result;
}

EXPORT
void nck_pace_set_dec_fb_timeout(struct nck_pace_dec *decoder, const struct timeval *dec_fb_timeout)
{
	if (dec_fb_timeout != NULL) {
		decoder->dec_fb_timeout = *dec_fb_timeout;
	} else {
		timerclear(&decoder->dec_fb_timeout);
	}
}

EXPORT
void nck_pace_set_dec_flush_timeout(struct nck_pace_dec *decoder, const struct timeval *dec_flush_timeout)
{
	if (dec_flush_timeout != NULL) {
		decoder->dec_flush_timeout = *dec_flush_timeout;
	} else {
		timerclear(&decoder->dec_flush_timeout);
	}
}

EXPORT
void nck_pace_dec_free(struct nck_pace_dec *decoder)
{
	krlnc_delete_decoder(decoder->coder);
	krlnc_delete_decoder_factory(decoder->factory);

	nck_timer_cancel(decoder->dec_fb_timeout_handle);
	nck_timer_free(decoder->dec_fb_timeout_handle);

	nck_timer_cancel(decoder->dec_flush_timeout_handle);
	nck_timer_free(decoder->dec_flush_timeout_handle);

	free(decoder->buffer);
	free(decoder->queue);
	free(decoder);
}

EXPORT
int nck_pace_dec_has_source(struct nck_pace_dec *decoder)
{
	if (decoder->queue_index < decoder->queue_length)
		return 1;

	if (decoder->index == decoder->symbols)
		return 0;

	return krlnc_decoder_is_symbol_uncoded(decoder->coder, decoder->index);
}

EXPORT
int nck_pace_dec_full(struct nck_pace_dec *decoder)
{
	return decoder->rank == decoder->symbols;
}

EXPORT
void nck_pace_dec_flush_source(struct nck_pace_dec *decoder)
{
	decoder->flush = 1;
	kodo_skip_undecoded(decoder->coder, &decoder->index);

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}
}

EXPORT
int nck_pace_dec_put_coded(struct nck_pace_dec *decoder, struct sk_buff *packet)
{
	uint32_t generation, rank;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (decoder->generation > generation) {
		// Encoder sending old gen packets. Should send feedback for each such packet??+
		// Right now, doing nothing.
		return 0;
	}

	if (decoder->generation < generation) {
		if (decoder->generation != 0){
			_flush_source(decoder);
			if (_has_source(decoder)) {
				nck_trigger_call(&decoder->on_source_ready);
			}
		}

		decoder->queue_index = 0;
		decoder->queue_length = kodo_flush_source(decoder->coder, decoder->buffer, decoder->index, decoder->queue);

		dec_start_next_generation(decoder, generation);
	}

	kodo_put_coded(decoder->coder, packet);

	decoder->rank = krlnc_decoder_rank(decoder->coder);

	if (((decoder->rank == decoder->prev_rank) && (decoder->prev_rank== rank)) ||
	    decoder->rank == decoder->symbols) {
		// No rank increase and the rank is same as the senders rank.
		// OR generation completed successfully. Send feedback.
		decoder->feedback_rank = decoder->rank;
		decoder->feedback_generation = decoder->generation;
		nck_trigger_call(&decoder->on_feedback_ready);
	}

	if (timerisset(&decoder->dec_fb_timeout)){			// todo: Check for correctness with use of '&'
		nck_timer_rearm(decoder->dec_fb_timeout_handle, &decoder->dec_fb_timeout);
	}

	if (timerisset(&decoder->dec_flush_timeout)){
		nck_timer_rearm(decoder->dec_flush_timeout_handle, &decoder->dec_flush_timeout);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	decoder->prev_rank = decoder->rank;

	return 0;
}

EXPORT
int nck_pace_dec_get_source(struct nck_pace_dec *decoder, struct sk_buff *packet)
{
	if (decoder->queue_length > decoder->queue_index) {
		uint8_t *payload = skb_put(packet, decoder->source_size);
		memcpy(payload, decoder->queue + decoder->source_size * decoder->queue_index, decoder->source_size);
		decoder->queue_index += 1;
		return 0;
	}

	return kodo_get_source(decoder->coder, packet, decoder->buffer, &decoder->index, decoder->flush);
}

EXPORT
int nck_pace_dec_get_feedback(struct nck_pace_dec *decoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	skb_push_u16(packet, (uint16_t)decoder->feedback_rank);
	skb_push_u32(packet, decoder->feedback_generation);

	decoder->feedback_generation = 0;

	return 0;
}

EXPORT
int nck_pace_dec_has_feedback(struct nck_pace_dec *decoder)
{
	return decoder->feedback_generation != 0;
}

EXPORT
int nck_pace_dec_complete(struct nck_pace_dec *decoder)
{
	// Redundant function -- Should be removed from the API?
	return nck_pace_dec_full(decoder);
}
