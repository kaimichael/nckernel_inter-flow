#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/chain.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>

#include "../private.h"

struct stage {
	unsigned int number;
	struct nck_chain_dec *parent;
	struct nck_decoder decoder;
};

struct nck_chain_dec {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	struct nck_decoder *first_stage;
	struct nck_decoder *last_stage;

	unsigned int stage_count;
	struct stage stages[];
};

NCK_DECODER_IMPL(nck_chain, NULL, NULL, NULL)

static int forward_decoded(struct nck_decoder *source, struct nck_decoder *dest, unsigned int size) {
	int packets = 0;
	uint8_t buffer[size];
	struct sk_buff skb;

	while (nck_has_source(source)) {
		skb_new(&skb, buffer, sizeof(buffer));
		if (nck_get_source(source, &skb)) {
			break;
		}

		nck_put_coded(dest, &skb);
		packets++;
	}

	return packets;
}

static void on_source_ready(void *c)
{
	struct stage *stage = (struct stage *)c;
	struct stage *next;

	if (stage->parent->first_stage == &stage->decoder) {
		nck_trigger_call(&stage->parent->on_source_ready);
		return;
	}

	assert(stage->number > 0);
	next = &stage->parent->stages[stage->number-1];

	assert(stage->decoder.source_size == next->decoder.coded_size);

	forward_decoded(&stage->decoder, &next->decoder, stage->decoder.source_size);
}

EXPORT
struct nck_chain_dec *nck_chain_dec(struct nck_decoder *stages, unsigned int stage_count)
{
	size_t feedback_size = 0;
	struct nck_chain_dec *result;
	size_t size = sizeof(*result) + stage_count*sizeof(struct stage);
	unsigned int i;

	result = malloc(size);
	memset(result, 0, size);
	result->stage_count = stage_count;

	for (i = 0; i < stage_count; ++i) {
		result->stages[i].number = i;
		result->stages[i].parent = result;
		result->stages[i].decoder = stages[i];

		nck_trigger_set(result->stages[i].decoder.on_source_ready, &result->stages[i], on_source_ready);

		if (feedback_size < result->stages[i].decoder.feedback_size) {
			feedback_size = result->stages[i].decoder.feedback_size;
		}
	}

	result->first_stage = &result->stages[0].decoder;
	result->last_stage = &result->stages[stage_count-1].decoder;

	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);
	result->source_size = result->first_stage->source_size;
	result->coded_size = result->last_stage->coded_size;
	result->feedback_size = feedback_size+1;

	return result;
}

EXPORT
int nck_chain_dec_set_stage_option(struct nck_chain_dec *decoder, unsigned int stage, const char *name, const char *value)
{
	if (stage >= decoder->stage_count) {
		return -1;
	}

	return nck_set_option(&decoder->stages[stage].decoder, name, value);
}

EXPORT
void nck_chain_dec_free(struct nck_chain_dec *decoder)
{
	unsigned int i = 0;
	for (i = 0; i < decoder->stage_count; ++i) {
		nck_free(&decoder->stages[i].decoder);
	}

	free(decoder);
}

EXPORT
int nck_chain_dec_has_source(struct nck_chain_dec *decoder)
{
	return nck_has_source(decoder->first_stage);
}

EXPORT
int nck_chain_dec_complete(struct nck_chain_dec *decoder)
{
	return nck_complete(decoder->first_stage);
}

EXPORT
void nck_chain_dec_flush_source(struct nck_chain_dec *decoder)
{
	unsigned int i;
	for (i = decoder->stage_count; i > 0; --i) {
		nck_flush_source(&decoder->stages[i-1].decoder);
	}
}

EXPORT
int nck_chain_dec_put_coded(struct nck_chain_dec *decoder, struct sk_buff *packet)
{
	return nck_put_coded(decoder->last_stage, packet);
}

EXPORT
int nck_chain_dec_get_source(struct nck_chain_dec *decoder, struct sk_buff *packet)
{
	return nck_get_source(decoder->first_stage, packet);
}

EXPORT
int nck_chain_dec_get_feedback(struct nck_chain_dec *decoder, struct sk_buff *packet)
{
	int ret;
	unsigned int i = 0;
	for (i = 0; i < decoder->stage_count; ++i) {
		if (nck_has_feedback(&decoder->stages[i].decoder)) {
			skb_reserve(packet, 1);
			ret = nck_get_feedback(&decoder->stages[i].decoder, packet);
			skb_push_u8(packet, i);
			return ret;
		}
	}
	return -1;
}

EXPORT
int nck_chain_dec_has_feedback(struct nck_chain_dec *decoder)
{
	unsigned int i = 0;
	for (i = 0; i < decoder->stage_count; ++i) {
		if (nck_has_feedback(&decoder->stages[i].decoder)) {
			return 1;
		}
	}
	return 0;
}
