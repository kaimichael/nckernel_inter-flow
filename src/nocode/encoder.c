#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/nocode.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/trace.h>

#include "../private.h"

struct nck_nocode_enc {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_coded_ready;

	size_t len;
	uint8_t buffer[];
};

NCK_ENCODER_IMPL(nck_nocode, NULL, NULL, NULL)

EXPORT
struct nck_nocode_enc *nck_nocode_enc(size_t symbol_size)
{
	struct nck_nocode_enc *result;

	result = malloc(sizeof(*result) + symbol_size);
	memset(result, 0, sizeof(*result) + symbol_size);
	nck_trigger_init(&result->on_coded_ready);
	result->source_size = symbol_size;
	result->coded_size = symbol_size;
	result->feedback_size = 0;

	result->len = 0;

	return result;
}

EXPORT
void nck_nocode_enc_free(struct nck_nocode_enc *encoder)
{
	free(encoder);
}

EXPORT
int nck_nocode_enc_has_coded(struct nck_nocode_enc *encoder)
{
	return encoder->len > 0;
}

EXPORT
int nck_nocode_enc_full(struct nck_nocode_enc *encoder)
{
	return encoder->len > 0;
}

EXPORT
int nck_nocode_enc_complete(struct nck_nocode_enc *encoder)
{
	return encoder->len == 0;
}

EXPORT
void nck_nocode_enc_flush_coded(struct nck_nocode_enc *encoder)
{
	UNUSED(encoder);
}

EXPORT
int nck_nocode_enc_put_source(struct nck_nocode_enc *encoder, struct sk_buff *packet)
{
	nck_trace(encoder, "%s", skb_str(packet));
	memcpy(encoder->buffer, packet->data, packet->len);

	encoder->len = packet->len;

	nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_nocode_enc_get_coded(struct nck_nocode_enc *encoder, struct sk_buff *packet)
{
	if (encoder->len == 0) {
		return -1;
	}

	uint8_t *payload = skb_put(packet, encoder->len);
	memcpy(payload, encoder->buffer, encoder->len);

	nck_trace(encoder, "%s", skb_str(packet));

	encoder->len = 0;

	return 0;
}

EXPORT
int nck_nocode_enc_put_feedback(struct nck_nocode_enc *encoder, struct sk_buff *packet)
{
	UNUSED(encoder);
	UNUSED(packet);
	return -1;
}
