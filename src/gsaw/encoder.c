#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/gsaw.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>

#include "../private.h"
#include "../kodo.h"

struct nck_gsaw_enc {
	krlnc_encoder_factory_t factory;
	krlnc_encoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;
	uint32_t block_size;
	uint32_t symbols;
	uint32_t rank;

	int full;
	int complete;
	uint32_t limit;
	int redundancy;

	struct nck_trigger on_coded_ready;

	uint8_t *buffer;
};

NCK_ENCODER_IMPL(nck_gsaw, NULL, NULL, NULL)

EXPORT
struct nck_gsaw_enc *nck_gsaw_enc(krlnc_encoder_factory_t factory, int redundancy)
{
	struct nck_gsaw_enc *result;
	uint32_t block_size;

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_coded_ready);

	result->generation = 1;
	result->symbols = krlnc_encoder_factory_symbols(factory);
	result->rank = 0;

	result->full = 0;
	result->complete = 0;
	result->limit = 0;
	result->redundancy = redundancy;

	result->factory = factory;
	result->coder = kodo_build_encoder(factory);

	block_size = krlnc_encoder_block_size(result->coder);
	result->buffer = malloc(block_size);
	memset(result->buffer, 0, block_size);

	result->source_size = krlnc_encoder_factory_symbol_size(factory);
	result->coded_size = krlnc_encoder_payload_size(result->coder) + 6;
	result->feedback_size = 6;

	result->on_coded_ready = (struct nck_trigger){0};

	return result;
}

EXPORT
void nck_gsaw_enc_free(struct nck_gsaw_enc *encoder)
{
	krlnc_delete_encoder(encoder->coder);
	krlnc_delete_encoder_factory(encoder->factory);
	free(encoder->buffer);
	free(encoder);
}

EXPORT
int nck_gsaw_enc_has_coded(struct nck_gsaw_enc *encoder)
{
	return encoder->limit >= encoder->symbols;
}

EXPORT
int nck_gsaw_enc_full(struct nck_gsaw_enc *encoder)
{
	return encoder->full;
}

EXPORT
int nck_gsaw_enc_complete(struct nck_gsaw_enc *encoder)
{
	return encoder->complete;
}

EXPORT
void nck_gsaw_enc_flush_coded(struct nck_gsaw_enc *encoder)
{
	encoder->limit += encoder->symbols + encoder->redundancy;
	encoder->full = 1;
	nck_trigger_call(&encoder->on_coded_ready);
}

EXPORT
int nck_gsaw_enc_put_source(struct nck_gsaw_enc *encoder, struct sk_buff *packet)
{
	if (encoder->complete) {
		encoder->generation++;
		encoder->rank = 0;
		encoder->complete = 0;
		encoder->full = 0;

		krlnc_delete_encoder(encoder->coder);
		encoder->coder = kodo_build_encoder(encoder->factory);
		memset(encoder->buffer, 0, krlnc_encoder_block_size(encoder->coder));
	}

	kodo_put_source(encoder->coder, packet, encoder->buffer, encoder->rank);

	encoder->rank++;

	if (encoder->rank == encoder->symbols) {
		_flush_coded(encoder);
	} else {
		encoder->limit += encoder->symbols + encoder->redundancy;
		nck_trigger_call(&encoder->on_coded_ready);
	}

	return 0;
}

EXPORT
int nck_gsaw_enc_get_coded(struct nck_gsaw_enc *encoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	kodo_encoder_get_coded(encoder->coder, packet);
	skb_push_u16(packet, encoder->rank);
	skb_push_u32(packet, encoder->generation);

	encoder->limit -= encoder->symbols;

	return 0;
}

EXPORT
int nck_gsaw_enc_put_feedback(struct nck_gsaw_enc *encoder, struct sk_buff *packet)
{
	uint32_t rank, generation, diff;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (generation != encoder->generation) {
		return 0;
	}

	if (rank >= encoder->rank) {
		// decoder has full information
		encoder->complete = 1;
		encoder->limit = encoder->limit % encoder->symbols;
		encoder->full = 0;
	} else {
		// we take the chance to recalculate the limit
		diff = encoder->rank - rank;
		encoder->limit += diff * (encoder->symbols + encoder->redundancy);
	}

	return 0;
}
