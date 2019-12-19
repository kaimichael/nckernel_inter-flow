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

struct nck_nocode_dec {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	size_t len;
	char buffer[];
};


NCK_DECODER_IMPL(nck_nocode, NULL, NULL, NULL)

EXPORT
struct nck_nocode_dec *nck_nocode_dec(size_t symbol_size)
{
	struct nck_nocode_dec *result;

	result = malloc(sizeof(*result) + symbol_size);
	memset(result, 0, sizeof(*result) + symbol_size);
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);
	result->source_size = symbol_size;
	result->coded_size = symbol_size;
	result->feedback_size = 0;

	result->len = 0;

	return result;
}

EXPORT
void nck_nocode_dec_free(struct nck_nocode_dec *decoder)
{
	free(decoder);
}

EXPORT
int nck_nocode_dec_has_source(struct nck_nocode_dec *decoder)
{
	return decoder->len > 0;
}

EXPORT
int nck_nocode_dec_complete(struct nck_nocode_dec *decoder)
{
	return decoder->len == 0;
}

EXPORT
void nck_nocode_dec_flush_source(struct nck_nocode_dec *decoder)
{
	UNUSED(decoder);
}

EXPORT
int nck_nocode_dec_put_coded(struct nck_nocode_dec *decoder, struct sk_buff *packet)
{
	nck_trace(decoder, "%s", skb_str(packet));
	memcpy(decoder->buffer, packet->data, packet->len);

	decoder->len = packet->len;

	nck_trigger_call(&decoder->on_source_ready);

	return 0;
}

EXPORT
int nck_nocode_dec_get_source(struct nck_nocode_dec *decoder, struct sk_buff *packet)
{
	if (decoder->len == 0) {
		return -1;
	}

	uint8_t *payload = skb_put(packet, decoder->len);
	memcpy(payload, decoder->buffer, decoder->len);

	nck_trace(decoder, "%s", skb_str(packet));

	decoder->len = 0;

	return 0;
}

EXPORT
int nck_nocode_dec_get_feedback(struct nck_nocode_dec *decoder, struct sk_buff *packet)
{
	UNUSED(decoder);
	UNUSED(packet);
	return -1;
}

EXPORT
int nck_nocode_dec_has_feedback(struct nck_nocode_dec *decoder)
{
	UNUSED(decoder);
	return 0;
}

