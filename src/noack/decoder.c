#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/noack.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"

struct nck_noack_dec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;

	uint32_t symbols;
	uint32_t index;

	int flush;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	uint8_t *buffer;

	uint8_t *queue;
	int queue_index;
	int queue_length;
};

NCK_DECODER_IMPL(nck_noack, nck_noack_dec_debug, NULL, NULL)

EXPORT
char *nck_noack_dec_debug(void *dec)
{
	static char debug[1024];
	struct nck_noack_dec *decoder = dec;

	snprintf(debug, sizeof(debug) - 1, "generation %d, index %d/%d, flush %d, rank %d, complete %d, has source %d",
			decoder->generation, decoder->index, decoder->symbols, decoder->flush,
			krlnc_decoder_rank(decoder->coder),
			_complete(decoder), _has_source(decoder)
			);
	debug[sizeof(debug) - 1] = 0;

	return debug;
}

static void decoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		struct nck_noack_dec *decoder = (struct nck_noack_dec *)context;
		_flush_source(decoder);
	}
}

EXPORT
struct nck_noack_dec *nck_noack_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer, const struct timeval *timeout)
{
	struct nck_noack_dec *result;
	uint32_t block_size;

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->generation = 0;

	result->symbols = krlnc_decoder_factory_symbols(factory);
	result->index = 0;
	result->flush = 0;

	result->timeout = *timeout;

	if (timer) {
		result->timeout_handle = nck_timer_add(timer, NULL, result, decoder_timeout_flush);
	} else {
		result->timeout_handle = 0;
	}

	result->factory = factory;
	result->coder = kodo_build_decoder(factory);

	block_size = krlnc_decoder_block_size(result->coder);
	result->buffer = malloc(block_size);
	memset(result->buffer, 0, block_size);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(result->coder) + 4;
	result->feedback_size = 0;

	result->queue = malloc(result->symbols * result->source_size);
	result->queue_index = 0;
	result->queue_length = 0;

	krlnc_decoder_set_mutable_symbols(result->coder, result->buffer, block_size);

	return result;
}

EXPORT
void nck_noack_dec_free(struct nck_noack_dec *decoder)
{
	krlnc_delete_decoder(decoder->coder);
	krlnc_delete_decoder_factory(decoder->factory);
	if (decoder->timeout_handle) {
		nck_timer_cancel(decoder->timeout_handle);
		nck_timer_free(decoder->timeout_handle);
	}
	free(decoder->buffer);
	free(decoder->queue);
	free(decoder);
}

EXPORT
int nck_noack_dec_has_source(struct nck_noack_dec *decoder)
{
	if (decoder->queue_index < decoder->queue_length)
		return 1;

	if (decoder->index == decoder->symbols)
		return 0;

	return krlnc_decoder_is_symbol_uncoded(decoder->coder, decoder->index);
}

EXPORT
int nck_noack_dec_complete(struct nck_noack_dec *decoder)
{
	return decoder->index == decoder->symbols;
}

EXPORT
void nck_noack_dec_flush_source(struct nck_noack_dec *decoder)
{
	if (decoder->flush)
		return;

	decoder->flush = 1;
	kodo_skip_undecoded(decoder->coder, &decoder->index);

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}
}

EXPORT
int nck_noack_dec_put_coded(struct nck_noack_dec *decoder, struct sk_buff *packet)
{
	uint32_t generation;

	if (packet->len < 4) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);

	/* Disregard old generations.
	 * TODO: Replace this dirty hack with something smarter, e.g.
	 * a timeout which allows "resetting" the generation only x seconds. */
	if (((int) (decoder->generation - generation)) > 0)
		return EINVAL;

	if (decoder->generation != generation) {
		if (!_complete(decoder) && !decoder->flush) {
			_flush_source(decoder);
			if (_has_source(decoder)) {
				nck_trigger_call(&decoder->on_source_ready);
			}
		}

		decoder->queue_index = 0;
		decoder->queue_length = kodo_flush_source(decoder->coder, decoder->buffer, decoder->index, decoder->queue);

		decoder->generation = generation;
		decoder->index = 0;
		decoder->flush = 0;

		krlnc_delete_decoder(decoder->coder);
		decoder->coder = kodo_build_decoder(decoder->factory);
		krlnc_decoder_set_mutable_symbols(decoder->coder, decoder->buffer, krlnc_decoder_block_size(decoder->coder));
	}

	kodo_put_coded(decoder->coder, packet);

	if (!decoder->flush && decoder->timeout_handle) {
		nck_timer_rearm(decoder->timeout_handle, &decoder->timeout);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_noack_dec_get_source(struct nck_noack_dec *decoder, struct sk_buff *packet)
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
int nck_noack_dec_get_feedback(struct nck_noack_dec *decoder, struct sk_buff *packet)
{
	UNUSED(decoder);
	UNUSED(packet);
	return -1;
}

EXPORT
int nck_noack_dec_has_feedback(struct nck_noack_dec *decoder)
{
	UNUSED(decoder);
	return 0;
}

