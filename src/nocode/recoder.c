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

struct nck_nocode_rec {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;
	struct nck_trigger on_coded_ready;

	int has_source, has_coded;

	size_t len;
	char buffer[];
};

NCK_RECODER_IMPL(nck_nocode, NULL, NULL, NULL)

EXPORT
struct nck_nocode_rec *nck_nocode_rec(size_t symbol_size)
{
	struct nck_nocode_rec *result;

	result = malloc(sizeof(*result) + symbol_size);
	memset(result, 0, sizeof(*result) + symbol_size);
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_coded_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->source_size = symbol_size;
	result->coded_size = symbol_size;
	result->feedback_size = 0;

	result->has_source = 0;
	result->has_coded = 0;
	result->len = 0;

	return result;
}

EXPORT
void nck_nocode_rec_free(struct nck_nocode_rec *recoder)
{
	free(recoder);
}


EXPORT
int nck_nocode_rec_complete(struct nck_nocode_rec *recoder)
{
	return !recoder->has_coded;
}

EXPORT
void nck_nocode_rec_flush_source(struct nck_nocode_rec *recoder)
{
	UNUSED(recoder);
}

EXPORT
void nck_nocode_rec_flush_coded(struct nck_nocode_rec *recoder)
{
	UNUSED(recoder);
}

EXPORT
int nck_nocode_rec_put_coded(struct nck_nocode_rec *recoder, struct sk_buff *packet)
{
	memcpy(recoder->buffer, packet->data, packet->len);

	recoder->has_source = 1;
	recoder->has_coded = 1;
	recoder->len = packet->len;

	nck_trigger_call(&recoder->on_source_ready);
	nck_trigger_call(&recoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_nocode_rec_has_coded(struct nck_nocode_rec *recoder)
{
	return recoder->has_coded;
}

EXPORT
int nck_nocode_rec_get_coded(struct nck_nocode_rec *recoder, struct sk_buff *packet)
{
	if (!recoder->has_coded) {
		return -1;
	}

	uint8_t *payload = skb_put(packet, recoder->len);
	memcpy(payload, recoder->buffer, recoder->len);

	recoder->has_coded = 0;

	nck_trace(recoder, "%s", skb_str(packet));

	return 0;
}

EXPORT
int nck_nocode_rec_has_source(struct nck_nocode_rec *recoder)
{
	return recoder->has_source;
}

EXPORT
int nck_nocode_rec_get_source(struct nck_nocode_rec *recoder, struct sk_buff *packet)
{
	if (!recoder->has_source) {
		return -1;
	}

	nck_trace(recoder, "%s", skb_str(packet));

	uint8_t *payload = skb_put(packet, recoder->len);
	memcpy(payload, recoder->buffer, recoder->len);

	recoder->has_source = 0;

	return 0;
}

EXPORT
int nck_nocode_rec_put_feedback(struct nck_nocode_rec *recoder, struct sk_buff *packet)
{
	UNUSED(recoder);
	UNUSED(packet);
	return -1;
}

EXPORT
int nck_nocode_rec_has_feedback(struct nck_nocode_rec *recoder)
{
	UNUSED(recoder);
	return 0;
}

EXPORT
int nck_nocode_rec_get_feedback(struct nck_nocode_rec *recoder, struct sk_buff *packet)
{
	UNUSED(recoder);
	UNUSED(packet);
	return -1;
}

