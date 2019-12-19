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

struct nck_noack_enc {
	krlnc_encoder_factory_t factory;
	krlnc_encoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;
	uint32_t symbols;
	uint32_t rank;

	int full;
	int complete;
	int limit;
	int redundancy;
	int systematic;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	struct nck_trigger on_coded_ready;

	uint8_t *buffer;
};

NCK_ENCODER_IMPL(nck_noack, NULL, NULL, NULL)

static void encoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		_flush_coded(context);
	}
}

EXPORT
struct nck_noack_enc *nck_noack_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer, int redundancy,
                                    int systematic, const struct timeval *timeout)
{
	struct nck_noack_enc *result;
	uint32_t block_size;


	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));

	result->full = 0;
	result->complete = 0;
	result->limit = 0;
	result->redundancy = redundancy;
	result->systematic = systematic;

	nck_trigger_init(&result->on_coded_ready);

	result->generation = 1;
	result->symbols = krlnc_encoder_factory_symbols(factory);
	result->rank = 0;

	result->timeout = *timeout;

	if (timer) {
		result->timeout_handle = nck_timer_add(timer, NULL, result, encoder_timeout_flush);
	} else {
		result->timeout_handle = 0;
	}

	result->factory = factory;
	result->coder = kodo_build_encoder(factory);
	if (!result->systematic) {
		krlnc_encoder_set_systematic_off(result->coder);
	}

	block_size = krlnc_encoder_block_size(result->coder);
	result->buffer = malloc(block_size);
	memset(result->buffer, 0, block_size);

	result->source_size = krlnc_encoder_factory_symbol_size(factory);
	result->coded_size = krlnc_encoder_payload_size(result->coder) + 4;
	result->feedback_size = 0;

	return result;
}

EXPORT
void nck_noack_set_redundancy(struct nck_noack_enc *encoder, int redundancy)
{
	encoder->redundancy = redundancy;
}

EXPORT
void nck_noack_enc_free(struct nck_noack_enc *encoder)
{
	krlnc_delete_encoder(encoder->coder);
	krlnc_delete_encoder_factory(encoder->factory);

	if (encoder->timeout_handle) {
		nck_timer_cancel(encoder->timeout_handle);
		nck_timer_free(encoder->timeout_handle);
	}

	free(encoder->buffer);
	free(encoder);
}

EXPORT
int nck_noack_enc_has_coded(struct nck_noack_enc *encoder)
{
	return encoder->limit > 0;
}

EXPORT
int nck_noack_enc_full(struct nck_noack_enc *encoder)
{
	return encoder->full;
}

EXPORT
int nck_noack_enc_complete(struct nck_noack_enc *encoder)
{
	return encoder->complete;
}

EXPORT
void nck_noack_enc_flush_coded(struct nck_noack_enc *encoder)
{
	encoder->limit += encoder->redundancy;
	encoder->full = 1;
	nck_trigger_call(&encoder->on_coded_ready);
}

EXPORT
int nck_noack_enc_put_source(struct nck_noack_enc *encoder, struct sk_buff *packet)
{
	if (encoder->complete) {
		encoder->generation++;
		encoder->rank = 0;
		encoder->complete = 0;
		encoder->full = 0;

		krlnc_delete_encoder(encoder->coder);
		encoder->coder = kodo_build_encoder(encoder->factory);
		if (!encoder->systematic) {
			krlnc_encoder_set_systematic_off(encoder->coder);
		}
		memset(encoder->buffer, 0, krlnc_encoder_block_size(encoder->coder));
	}

	kodo_put_source(encoder->coder, packet, encoder->buffer, encoder->rank);

	encoder->rank++;
	encoder->limit++;

	if (encoder->rank == encoder->symbols) {
		_flush_coded(encoder);
	}

	if (encoder->timeout_handle) {
		nck_timer_rearm(encoder->timeout_handle, &encoder->timeout);
	}

	nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_noack_enc_get_coded(struct nck_noack_enc *encoder, struct sk_buff *packet)
{
	skb_reserve(packet, 4);

	kodo_encoder_get_coded(encoder->coder, packet);
	skb_push_u32(packet, encoder->generation);

	encoder->limit--;

	if (encoder->limit == 0 && encoder->full) {
		encoder->complete = 1;
		encoder->full = 0;
	}

	return 0;
}

EXPORT
int nck_noack_enc_put_feedback(struct nck_noack_enc *encoder, struct sk_buff *packet)
{
	UNUSED(encoder);
	UNUSED(packet);
	return -1;
}
