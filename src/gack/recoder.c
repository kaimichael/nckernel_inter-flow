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

struct nck_gack_rec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;
	uint32_t symbols;
	uint32_t index;
	uint32_t sender_rank;

	uint32_t feedback_generation;

	uint32_t feedback_rank;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_coded_ready;
	struct nck_trigger on_feedback_ready;

	int flush;
	int has_source;
	int has_coded;
	int complete;

	uint8_t *buffer;
};

NCK_RECODER_IMPL(nck_gack, NULL, NULL, NULL)

static void rec_reset(struct nck_gack_rec *recoder, uint32_t generation)
{
	recoder->generation = generation;
	recoder->sender_rank = 0;
	recoder->index = 0;
	recoder->flush = 0;
	recoder->has_source = 0;
	recoder->has_coded = 0;

	krlnc_delete_decoder(recoder->coder);
	recoder->coder = kodo_build_decoder(recoder->factory);
	krlnc_decoder_set_mutable_symbols(recoder->coder, recoder->buffer, krlnc_decoder_block_size(recoder->coder));
}

EXPORT
struct nck_gack_rec *nck_gack_rec(krlnc_decoder_factory_t factory)
{
	struct nck_gack_rec *result;
	uint32_t block_size;

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_coded_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->generation = 0;

	result->symbols = krlnc_decoder_factory_symbols(factory);
	result->index = 0;
	result->flush = 0;

	result->sender_rank = 0;
	result->complete = 0;
	result->has_source = 0;
	result->has_coded = 0;

	result->feedback_generation = 0;
	result->feedback_rank = 0;

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
	result->on_coded_ready = (struct nck_trigger){0};

	return result;
}

EXPORT
void nck_gack_rec_free(struct nck_gack_rec *recoder)
{
	krlnc_delete_decoder(recoder->coder);
	krlnc_delete_decoder_factory(recoder->factory);
	free(recoder->buffer);
	free(recoder);
}

/**
 * Returns true when there are no packets received by the recoder yet, i.e. rank=0.
 */
EXPORT
int nck_gack_rec_has_coded(struct nck_gack_rec *recoder)
{
	return recoder->has_coded;
}

EXPORT
int nck_gack_rec_has_source(struct nck_gack_rec *recoder)
{
	return recoder->has_source;
}

/**
 * Returns complete if the recoder recieves appropriate feedback that the successor is complete.
 */
EXPORT
int nck_gack_rec_complete(struct nck_gack_rec *recoder)
{
	return recoder->complete;
}

EXPORT
void nck_gack_rec_flush_coded(struct nck_gack_rec *recoder)
{
	UNUSED(recoder);
}

EXPORT
void nck_gack_rec_flush_source(struct nck_gack_rec *recoder)
{
	recoder->flush = 1;
	kodo_skip_undecoded(recoder->coder, &(recoder->index));
}

EXPORT
int nck_gack_rec_put_coded(struct nck_gack_rec *recoder, struct sk_buff *packet)
{
	uint32_t generation;
	uint32_t sender_rank;
	uint32_t prev_rank;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	sender_rank = skb_pull_u16(packet);

	if (recoder->generation == 0) {
		// we have not yet received anything, so we take the senders generation
		recoder->generation = generation;
	} else if (generation > recoder->generation) {
		// if we receive something from a later generation then we reset
		rec_reset(recoder, generation);
	} else if (recoder->generation < generation) {
		// in this case we received a delayed packet
		// we can safely send some feedback here
		recoder->feedback_generation = generation;
		recoder->feedback_rank = recoder->symbols;
		nck_trigger_call(&recoder->on_feedback_ready);
		return 0;
	}

	prev_rank = krlnc_decoder_rank(recoder->coder);
	kodo_put_coded(recoder->coder, packet);

	if (!recoder->has_coded) {
		// if we are not sending coded packets already
		if (recoder->sender_rank == sender_rank) {
			// if the sender rank has not increased then we issue
			// some feedback
			recoder->feedback_generation = generation;
			recoder->feedback_rank = sender_rank;
			nck_trigger_call(&recoder->on_feedback_ready);
		} else if (recoder->sender_rank < sender_rank		// if the sender has new information
			   && prev_rank < krlnc_decoder_rank(recoder->coder)	// and we received some new information
			  ) {
			recoder->sender_rank = sender_rank;
			recoder->has_coded = 1;
			nck_trigger_call(&recoder->on_coded_ready);
		}
	}

	if (recoder->has_source) {
		nck_trigger_call(&recoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_gack_rec_get_coded(struct nck_gack_rec *recoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	kodo_decoder_get_coded(recoder->coder, packet);
	skb_push_u16(packet, (uint16_t)recoder->sender_rank);
	skb_push_u32(packet, recoder->generation);

	return 0;
}

EXPORT
int nck_gack_rec_get_source(struct nck_gack_rec *recoder, struct sk_buff *packet)
{
	return kodo_get_source(recoder->coder, packet, recoder->buffer, &recoder->index, recoder->flush);
}

EXPORT
int nck_gack_rec_get_feedback(struct nck_gack_rec *recoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	skb_push_u16(packet, (uint16_t)recoder->feedback_rank);
	skb_push_u32(packet, recoder->feedback_generation);

	recoder->feedback_generation = 0;

	return 0;
}

EXPORT
int nck_gack_rec_put_feedback(struct nck_gack_rec *recoder, struct sk_buff *packet)
{
	uint32_t rank, generation;

	// feedback comes either from a decoder or from a recoder
	// feedback will only be sent if the decoder can decode all symbols

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (generation > recoder->generation) {
		// If there is some feedback flowing around with a generation
		// later than our generation, then we should shut up.
		// Also we reset to the given generation. This will cause
		// forwarding of feedback if someone sends outdated packets to
		// this recoder.
		rec_reset(recoder, generation);
		return 0;
	} else if (generation < recoder->generation) {
		// in this case we have a late feedback and we just ignore it
		return 0;
	}

	// now we know for sure that the feedback is for the current generation
	if (rank >= recoder->sender_rank) {
		// if a feedback arrives here with a rank higher then our
		// currently known sender rank, then we better shut up and wait
		// for new data
		recoder->sender_rank = rank;
		recoder->complete = 1;
		recoder->has_coded = 0;

		// and we should issue some feedback
		recoder->feedback_generation = generation;
		recoder->feedback_rank = rank;
		nck_trigger_call(&recoder->on_feedback_ready);
		return 0;
	}

	return 0;
}

EXPORT
int nck_gack_rec_has_feedback(struct nck_gack_rec *recoder)
{
	return recoder->feedback_generation != 0;
}

