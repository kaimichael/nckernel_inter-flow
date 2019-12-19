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

struct nck_noack_rec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	size_t source_size, coded_size, feedback_size;

	uint32_t generation;
	uint32_t symbols;
	uint32_t rank;
	uint32_t index;

	int complete;
	int limit;
	int redundancy;
	int flush;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;

	struct nck_trigger on_coded_ready;
	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	uint8_t *buffer;
};

NCK_RECODER_IMPL(nck_noack, NULL, NULL, NULL)

static void recoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		_flush_coded(context);
		_flush_source(context);
	}
}

EXPORT
struct nck_noack_rec *nck_noack_rec(krlnc_decoder_factory_t factory,
				    struct nck_timer *timer, int redundancy,
				    const struct timeval *timeout)
{
	struct nck_noack_rec *result;
	uint32_t block_size;

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_coded_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->complete = 0;
	result->limit = 0;
	result->flush = 0;
	result->index = 0;
	result->redundancy = redundancy;

	result->timeout = *timeout;

	if (timer) {
		result->timeout_handle = nck_timer_add(timer, NULL, result, recoder_timeout_flush);
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

	result->generation = 1;
	result->symbols = krlnc_decoder_factory_symbols(factory);
	result->rank = 0;

	krlnc_decoder_set_mutable_symbols(result->coder, result->buffer, block_size);

	return result;
}

EXPORT
void nck_noack_rec_free(struct nck_noack_rec *recoder)
{
	krlnc_delete_decoder(recoder->coder);
	krlnc_delete_decoder_factory(recoder->factory);

	if (recoder->timeout_handle) {
		nck_timer_cancel(recoder->timeout_handle);
		nck_timer_free(recoder->timeout_handle);
	}

	free(recoder->buffer);
	free(recoder);
}


EXPORT
int nck_noack_rec_complete(struct nck_noack_rec *recoder)
{
	return recoder->complete;
}

EXPORT
void nck_noack_rec_flush_source(struct nck_noack_rec *recoder)
{
	recoder->flush = 1;
}

EXPORT
void nck_noack_rec_flush_coded(struct nck_noack_rec *recoder)
{
	recoder->limit += recoder->redundancy;
	nck_trigger_call(&recoder->on_coded_ready);
}

EXPORT
int nck_noack_rec_put_coded(struct nck_noack_rec *recoder,
			    struct sk_buff *packet)
{
	uint32_t generation;
	uint32_t prev_rank;

	if (packet->len < 4) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);

	/* Disregard old generations.
	 *
	 * TODO: Replace this dirty hack with something smarter, e.g.
	 * a timeout which allows "resetting" the generation only x seconds. */
	if (((int) (recoder->generation - generation)) > 0)
		return EINVAL;

	if (recoder->generation != generation) {
		_flush_source(recoder);
		if (_has_source(recoder)) {
			nck_trigger_call(&recoder->on_source_ready);
		}
		_flush_coded(recoder);
		if (_has_coded(recoder)) {
			nck_trigger_call(&recoder->on_coded_ready);
		}
		recoder->generation = generation;
		recoder->index = 0;
		recoder->flush = 0;
		recoder->limit = 0;

		krlnc_delete_decoder(recoder->coder);
		recoder->coder = kodo_build_decoder(recoder->factory);
		krlnc_decoder_set_mutable_symbols(recoder->coder, recoder->buffer, krlnc_decoder_block_size(recoder->coder));
	}

	prev_rank = krlnc_decoder_rank(recoder->coder);
	kodo_put_coded(recoder->coder, packet);

	if (!recoder->flush && recoder->timeout_handle) {
		nck_timer_rearm(recoder->timeout_handle, &recoder->timeout);
	}

	if (prev_rank < krlnc_decoder_rank(recoder->coder)) {
		// rank can increase at most bye one
		recoder->limit += 1;
		nck_trigger_call(&recoder->on_coded_ready);
	}

	if (_has_source(recoder)) {
		nck_trigger_call(&recoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_noack_rec_has_coded(struct nck_noack_rec *recoder)
{
	return recoder->limit > 0;
}

EXPORT
int nck_noack_rec_get_coded(struct nck_noack_rec *recoder,
			    struct sk_buff *packet)
{
	skb_reserve(packet, 4);
	kodo_decoder_get_coded(recoder->coder, packet);
	skb_push_u32(packet, recoder->generation);

	recoder->limit--;

	if (recoder->limit == 0) {
		recoder->complete = 1;
	}

	return 0;
}

EXPORT
int nck_noack_rec_has_source(struct nck_noack_rec *recoder)
{
	if (recoder->index == recoder->symbols)
		return 0;

	return krlnc_decoder_is_symbol_uncoded(recoder->coder, recoder->index);
}

EXPORT
int nck_noack_rec_get_source(struct nck_noack_rec *recoder,
			     struct sk_buff *packet)
{
	return kodo_get_source(recoder->coder, packet, recoder->buffer, &recoder->index, recoder->flush);
}

EXPORT
int nck_noack_rec_put_feedback(struct nck_noack_rec *recoder,
			       struct sk_buff *packet)
{
	UNUSED(recoder);
	UNUSED(packet);
	return -1;
}

EXPORT
int nck_noack_rec_has_feedback(struct nck_noack_rec *recoder)
{
	UNUSED(recoder);
	return 0;
}

EXPORT
int nck_noack_rec_get_feedback(struct nck_noack_rec *recoder,
			       struct sk_buff *packet)
{
	UNUSED(recoder);
	UNUSED(packet);
	return -1;
}

