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

// this structure is used to have all information available for a callback
struct stage {
	unsigned int number; // current stage number
	struct nck_chain_enc *parent; // pointer to the container
	struct nck_encoder encoder; // encoder for this stage
};

struct nck_chain_enc {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_coded_ready;

	struct nck_encoder *first_stage; // shortcut to the first stage
	struct nck_encoder *last_stage; // shortcut to the last stage

	unsigned int stage_count; // number of items in the following array
	struct stage stages[]; // dynamically sized array of stages
};

NCK_ENCODER_IMPL(nck_chain, NULL, NULL, NULL)

/**
 * forward_coded - get packets from one encoder and put it to the next
 */
static int forward_coded(struct nck_encoder *source, struct nck_encoder *dest, unsigned int size) {
	int packets = 0;
	uint8_t buffer[size];
	struct sk_buff skb;

	// forward all packets to the next encoder until the next
	// encoder is either full or we have no more packets
	while (nck_has_coded(source) && !nck_full(dest)) {
		skb_new(&skb, buffer, sizeof(buffer));
		if (nck_get_coded(source, &skb)) {
			break;
		}

		nck_put_source(dest, &skb);
		packets++;
	}

	return packets;
}

static void on_coded_ready(void *c)
{
	struct stage *stage = (struct stage *)c;
	struct stage *next;

	// if the callback was triggered by the last stage
	// then we propagate the call to the parent encoder
	if (stage->parent->last_stage == &stage->encoder) {
		nck_trigger_call(&stage->parent->on_coded_ready);
		return;
	}

	// for everything else we should propagate to the next stage
	assert(stage->number+1 < stage->parent->stage_count);
	next = &stage->parent->stages[stage->number+1];

	forward_coded(&stage->encoder, &next->encoder, stage->encoder.coded_size);
}

EXPORT
struct nck_chain_enc *nck_chain_enc(struct nck_encoder *stages, unsigned int stage_count)
{
	size_t feedback_size = 0;
	struct nck_chain_enc *result;
	size_t size = sizeof(*result) + stage_count*sizeof(struct stage);
	unsigned int i;

	result = malloc(size);
	memset(result, 0, size);
	result->stage_count = stage_count;

	for (i = 0; i < stage_count; ++i) {
		// messages from the previous encoder should fit into the current encoder
		assert(i == 0 || stages[i].source_size <= stages[i-1].coded_size);

		// populate the stage structure with all information
		// required to process the callback
		result->stages[i].number = i;
		result->stages[i].parent = result;
		result->stages[i].encoder = stages[i];

		// register the callback to pass coded packets from one stage to the next
		nck_trigger_set(result->stages[i].encoder.on_coded_ready, &result->stages[i], on_coded_ready);

		// we need to be able to process the biggest feedback
		feedback_size = max_t(size_t, feedback_size, result->stages[i].encoder.feedback_size);
	}

	// first and last encoder are the most frequently used
	// so we remember a shortcut to them
	result->first_stage = &result->stages[0].encoder;
	result->last_stage = &result->stages[stage_count-1].encoder;

	nck_trigger_init(&result->on_coded_ready);

	// our input should fit into the first encoder
	result->source_size = result->first_stage->source_size;
	// our output is as big as from the last encoder
	result->coded_size = result->last_stage->coded_size;
	// feedback is the biggest feedback plus a stage id
	result->feedback_size = feedback_size+1;

	return result;
}

EXPORT
int nck_chain_enc_set_stage_option(struct nck_chain_enc *encoder, unsigned int stage, const char *name, const char *value)
{
	if (stage >= encoder->stage_count) {
		return -1;
	}

	// lookup the correct stage and forward the configuration
	return nck_set_option(&encoder->stages[stage].encoder, name, value);
}

EXPORT
void nck_chain_enc_free(struct nck_chain_enc *encoder)
{
	unsigned int i = 0;
	// free all encoders in sub stages
	for (i = 0; i < encoder->stage_count; ++i) {
		nck_free(&encoder->stages[i].encoder);
	}

	free(encoder);
}

EXPORT
int nck_chain_enc_has_coded(struct nck_chain_enc *encoder)
{
	return nck_has_coded(encoder->last_stage);
}

EXPORT
int nck_chain_enc_full(struct nck_chain_enc *encoder)
{
	return nck_full(encoder->first_stage);
}

EXPORT
int nck_chain_enc_complete(struct nck_chain_enc *encoder)
{
	return nck_complete(encoder->first_stage);
}

EXPORT
void nck_chain_enc_flush_coded(struct nck_chain_enc *encoder)
{
	unsigned int i;
	for (i = 0; i < encoder->stage_count; ++i) {
		nck_flush_coded(&encoder->stages[i].encoder);
	}
}

EXPORT
int nck_chain_enc_put_source(struct nck_chain_enc *encoder, struct sk_buff *packet)
{
	return nck_put_source(encoder->first_stage, packet);
}

/**
 * push_coded - try to push more coded packets down the chain
 *
 * @encoder: pointer to encoder chain
 * @stage: stage from where to start pushing
 */
static void push_coded(struct nck_chain_enc *encoder, unsigned int stage)
{
	struct nck_encoder *source;
	struct nck_encoder *dest;

	// iterate from the initial stage up to the first encoder
	for (; stage > 0; --stage) {
		source = &encoder->stages[stage-1].encoder;
		dest = &encoder->stages[stage].encoder;

		// try to add some packets from the previous controller
		if (forward_coded(source, dest, source->coded_size) == 0) {
			// If nothing was pushed then probably nothing has changed
			// in the source encoder. So there is no reason to check
			// further upwards.
			break;
		}

		// Otherwise by pushing coded packets the source encoder maybe
		// has some capacity to get coded packets from the previous encoder.
	}
}

EXPORT
int nck_chain_enc_get_coded(struct nck_chain_enc *encoder, struct sk_buff *packet)
{
	int ret;
	ret = nck_get_coded(encoder->last_stage, packet);
	// getting a coded packet could free up some space in the last encoder
	// so we might be able to push more coded packets down the chain
	push_coded(encoder, encoder->stage_count-1);
	return ret;
}

EXPORT
int nck_chain_enc_put_feedback(struct nck_chain_enc *encoder, struct sk_buff *packet)
{
	int ret;
	unsigned int stage = skb_pull_u8(packet);

	if (stage > encoder->stage_count) {
		return -1;
	}

	ret = nck_put_feedback(&encoder->stages[stage].encoder, packet);
	// getting feedback might free up some space in the encoder
	// so we might be able to push more coded packets down the chain
	push_coded(encoder, stage);
	return ret;
}
