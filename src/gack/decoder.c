#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/gack.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>

#include "../private.h"
#include "../kodo.h"

struct nck_gack_dec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;

	uint32_t symbols;
	uint32_t index;

	int flush;

	uint32_t feedback_generation;
	uint32_t feedback_rank;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	uint8_t *buffer;
};

NCK_DECODER_IMPL(nck_gack, nck_gack_dec_debug, NULL, NULL)

EXPORT
char *nck_gack_dec_debug(void *dec)
{
	static char debug[1024];
	struct nck_gack_dec *decoder = dec;

	snprintf(debug, sizeof(debug) - 1, "generation %d, index %d, flush %d, rank %d, complete %d, has source %d",
		decoder->generation, decoder->index, decoder->flush,
		krlnc_decoder_rank(decoder->coder),
		_complete(decoder), _has_source(decoder)
		);
	debug[sizeof(debug) - 1] = 0;

	return debug;
}

EXPORT
struct nck_gack_dec *nck_gack_dec(krlnc_decoder_factory_t factory)
{
	struct nck_gack_dec *result;
	uint32_t block_size;

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->generation = 0;

	result->symbols = krlnc_decoder_factory_symbols(factory);
	result->index = 0;
	result->flush = 0;

	result->feedback_generation = 0;

	result->factory = factory;
	result->coder = kodo_build_decoder(factory);

	block_size = krlnc_decoder_block_size(result->coder);
	result->buffer = malloc(block_size);
	memset(result->buffer, 0, sizeof(block_size));
	krlnc_decoder_set_mutable_symbols(result->coder, result->buffer, block_size);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(result->coder) + 6;
	result->feedback_size = 6;

	result->on_source_ready = (struct nck_trigger){0};
	result->on_feedback_ready = (struct nck_trigger){0};

	return result;
}

EXPORT
void nck_gack_dec_free(struct nck_gack_dec *decoder)
{
	krlnc_delete_decoder(decoder->coder);
	krlnc_delete_decoder_factory(decoder->factory);
	free(decoder->buffer);
	free(decoder);
}

EXPORT
int nck_gack_dec_has_source(struct nck_gack_dec *decoder)
{
	if (decoder->index == decoder->symbols) {
		return 0;
	}

	return krlnc_decoder_is_symbol_uncoded(decoder->coder, decoder->index);
}

EXPORT
int nck_gack_dec_complete(struct nck_gack_dec *decoder)
{
	return decoder->index == decoder->symbols;
}

EXPORT
void nck_gack_dec_flush_source(struct nck_gack_dec *decoder)
{
	decoder->flush = 1;
	kodo_skip_undecoded(decoder->coder, &(decoder->index));
}

EXPORT
int nck_gack_dec_put_coded(struct nck_gack_dec *decoder, struct sk_buff *packet)
{
	uint32_t generation;
	uint32_t rank;

	if (packet->len < 6) {
		return ENOSPC;
	}
	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (_complete(decoder)) {
		decoder->generation = decoder->generation + 1;
		decoder->index = 0;
		decoder->flush = 0;

		krlnc_delete_decoder(decoder->coder);
		decoder->coder = kodo_build_decoder(decoder->factory);
		krlnc_decoder_set_mutable_symbols(decoder->coder, decoder->buffer, krlnc_decoder_block_size(decoder->coder));
	}

	if (decoder->generation == 0) {
		/* we have not yet received anything, so we take the senders generation */
		decoder->generation = generation;
	} else if (decoder->generation < generation) {
		decoder->feedback_generation = generation;
		decoder->feedback_rank = decoder->symbols;
		nck_trigger_call(&decoder->on_feedback_ready);
		return 0;
	} else if (decoder->generation > generation) {
		return 0;
	}

	kodo_put_coded(decoder->coder, packet);

	if (rank <= krlnc_decoder_rank(decoder->coder)) {
		decoder->feedback_generation = generation;
		decoder->feedback_rank = rank;
		nck_trigger_call(&decoder->on_feedback_ready);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_gack_dec_get_source(struct nck_gack_dec *decoder, struct sk_buff *packet)
{
	return kodo_get_source(decoder->coder, packet, decoder->buffer, &decoder->index, decoder->flush);
}

EXPORT
int nck_gack_dec_get_feedback(struct nck_gack_dec *decoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	skb_push_u16(packet, (uint16_t)decoder->feedback_rank);
	skb_push_u32(packet, decoder->feedback_generation);

	decoder->feedback_generation = 0;

	return 0;
}

EXPORT
int nck_gack_dec_has_feedback(struct nck_gack_dec *decoder)
{
	return decoder->feedback_generation != 0;
}

