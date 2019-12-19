#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/pace.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"


struct nck_pace_rec {
	krlnc_decoder_factory_t factory;
	krlnc_decoder_t coder;

	uint32_t block_size;
	uint32_t symbols;

	uint32_t generation;
	uint32_t rank;
	uint32_t prev_rank;

	uint16_t pace_redundancy;
	uint16_t tail_redundancy;

	size_t source_size, coded_size, feedback_size;

	uint32_t index;

	struct nck_timer *timer; 				// should be pointer?
	struct timeval rec_fb_timeout;
	struct nck_timer_entry *rec_fb_timeout_handle;
	struct timeval rec_flush_timeout;
	struct nck_timer_entry *rec_flush_timeout_handle;
	struct timeval rec_redundancy_timeout;
	struct nck_timer_entry *rec_redundancy_timeout_handle;

	struct nck_trigger on_source_ready, on_coded_ready, on_feedback_ready;

	uint32_t feedback_generation;
	uint32_t feedback_rank;

	uint32_t last_sender_rank;

	uint32_t last_fb_rank;
	uint32_t to_send;

	int flush;

	uint8_t *buffer;

    uint8_t *queue;
    int queue_index;
    int queue_length;
};

NCK_RECODER_IMPL(nck_pace, NULL, NULL, NULL)

static void rec_start_next_generation(struct nck_pace_rec *recoder, uint32_t generation)
{
	recoder->generation = generation;
	recoder->rank = 0;
	recoder->flush = 0;
	recoder->index = 0;
	recoder->last_sender_rank = 0;
	recoder->prev_rank = 0;

	recoder->to_send = 0;
	recoder->last_fb_rank = 0;

	recoder->feedback_rank = 0;
	recoder->feedback_generation=0;

	if (recoder->coder){
		krlnc_delete_decoder(recoder->coder);
	}
	recoder->coder = kodo_build_decoder(recoder->factory);
	memset(recoder->buffer, 0, recoder->block_size);
	krlnc_decoder_set_mutable_symbols(recoder->coder, recoder->buffer, recoder->block_size);
}


static void recoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success){
	UNUSED(entry);

	if (success) {
		struct nck_pace_rec *recoder = (struct nck_pace_rec *) context;
		UNUSED(recoder);

		//todo: is this the right thing to do? How do we recreate the Encoder when put_src is called later?
		// nck_pace_rec_free(recoder);
	}
}

static void recoder_send_feedback(struct nck_timer_entry *entry, void *context, int success){
	UNUSED(entry);

	if (success) {
		struct nck_pace_rec *recoder = (struct nck_pace_rec *)context;

		recoder->feedback_rank = recoder->rank;
		recoder->feedback_generation = recoder->generation;
		nck_trigger_call(&recoder->on_feedback_ready);
	}
}

static void recoder_send_redundancy(struct nck_timer_entry *entry, void *context, int success){
	UNUSED(entry);

	if (success) {
		struct nck_pace_rec *recoder = (struct nck_pace_rec *)context;

		if (recoder->rank > recoder->last_fb_rank){
			recoder->to_send += recoder->pace_redundancy;
			nck_trigger_call(&recoder->on_coded_ready);

			nck_timer_rearm(recoder->rec_fb_timeout_handle, &recoder->rec_fb_timeout);
//			fprintf(stderr, "\nSending redundancy");
		}

	}
}


EXPORT
struct nck_pace_rec *nck_pace_rec(krlnc_decoder_factory_t factory, struct nck_timer *timer)
{
	struct nck_pace_rec *result;
	uint32_t block_size;
	krlnc_decoder_t dec;

	dec = kodo_build_decoder(factory);
	block_size = krlnc_decoder_block_size(dec);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_coded_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->buffer = malloc(block_size);

	result->symbols = krlnc_decoder_factory_symbols(factory);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(dec) + 6;
	result->feedback_size = 6;

	result->factory = factory;
	result->block_size = block_size;

	result->queue = malloc(result->symbols * result->source_size);
	result->queue_index = 0;
	result->queue_length = 0;
	rec_start_next_generation(result, 1);

	result->timer = timer;

	result->rec_fb_timeout_handle = nck_timer_add(timer, NULL, result, recoder_send_feedback);
	result->rec_redundancy_timeout_handle = nck_timer_add(timer, NULL, result, recoder_send_redundancy);
	result->rec_flush_timeout_handle = nck_timer_add(timer, NULL, result, recoder_timeout_flush);

	struct timeval timeout_fb, timeout_flush, timeout_redundancy;

	nck_pace_set_rec_fb_timeout(result, double_to_tv(0.050, &timeout_fb));
	nck_pace_set_rec_flush_timeout(result, double_to_tv(60.0, &timeout_flush));
	nck_pace_set_rec_redundancy_timeout(result, double_to_tv(60.0, &timeout_redundancy));

	nck_pace_set_rec_pace_redundancy(result, 120);
	nck_pace_set_rec_tail_redundancy(result, 100);

	krlnc_delete_decoder(dec);

	return result;
}

EXPORT
void nck_pace_set_rec_fb_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_fb_timeout)
{
	if (rec_fb_timeout != NULL) {
		recoder->rec_fb_timeout = *rec_fb_timeout;
	} else {
		timerclear(&recoder->rec_fb_timeout);
	}
}

EXPORT
void nck_pace_set_rec_flush_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_flush_timeout)
{
	if (rec_flush_timeout != NULL) {
		recoder->rec_flush_timeout = *rec_flush_timeout;
	} else {
		timerclear(&recoder->rec_flush_timeout);
	}
}

EXPORT
void nck_pace_set_rec_redundancy_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_fb_timeout)
{
	if (rec_fb_timeout != NULL) {
		recoder->rec_redundancy_timeout = *rec_fb_timeout;
	} else {
		timerclear(&recoder->rec_redundancy_timeout);
	}
}

EXPORT
void nck_pace_set_rec_pace_redundancy(struct nck_pace_rec *recoder, uint16_t pace_redundancy)
{
	recoder->pace_redundancy = pace_redundancy;
}

EXPORT
void nck_pace_set_rec_tail_redundancy(struct nck_pace_rec *recoder, uint16_t tail_redundancy)
{
	recoder->tail_redundancy = tail_redundancy;
}


EXPORT
void nck_pace_rec_free(struct nck_pace_rec *recoder)
{
	krlnc_delete_decoder(recoder->coder);
	krlnc_delete_decoder_factory(recoder->factory);

	nck_timer_cancel(recoder->rec_fb_timeout_handle);
	nck_timer_free(recoder->rec_fb_timeout_handle);

	nck_timer_cancel(recoder->rec_redundancy_timeout_handle);
	nck_timer_free(recoder->rec_redundancy_timeout_handle);

	nck_timer_cancel(recoder->rec_flush_timeout_handle);
	nck_timer_free(recoder->rec_flush_timeout_handle);

	free(recoder->buffer);
    	free(recoder->queue);
	free(recoder);
}

EXPORT
int nck_pace_rec_has_source(struct nck_pace_rec *recoder)
{
    if (recoder->queue_index < recoder->queue_length)
        return 1;

    if (recoder->index == recoder->symbols)
        return 0;

    return krlnc_decoder_is_symbol_uncoded(recoder->coder, recoder->index);
}

EXPORT
int nck_pace_rec_full(struct nck_pace_rec *recoder)
{
	int ret;
	ret = recoder->rank == recoder->symbols &&
		  recoder->to_send >= 100;
	return ret;
}

EXPORT
void nck_pace_rec_flush_source(struct nck_pace_rec *recoder)
{
	recoder->flush = 1;
	kodo_skip_undecoded(recoder->coder, &recoder->index);

	if (_has_source(recoder)) {
		nck_trigger_call(&recoder->on_source_ready);
	}
}

EXPORT
void nck_pace_rec_flush_coded(struct nck_pace_rec *recoder)
{
	recoder->to_send += recoder->tail_redundancy;
	nck_trigger_call(&recoder->on_coded_ready);
}

EXPORT
int nck_pace_rec_put_coded(struct nck_pace_rec *recoder, struct sk_buff *packet)
{
	//todo:recheck logic
	uint32_t generation, rank;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (recoder->generation > generation) {
		// Encoder sending old gen packets. Should send feedback for each such packet??+
		// Right now, doing nothing.
		return 0;
	}

	//todo:handle overflow
	if (recoder->generation < generation) {
		if (recoder->generation != 0){
			_flush_source(recoder);
			_flush_coded(recoder);
			if (_has_source(recoder)) {
				nck_trigger_call(&recoder->on_source_ready);
			}
		}
        recoder->queue_index = 0;
        recoder->queue_length = kodo_flush_source(recoder->coder, recoder->buffer, recoder->index, recoder->queue);
		rec_start_next_generation(recoder, generation);
	}

	kodo_put_coded(recoder->coder, packet);

	recoder->rank = krlnc_decoder_rank(recoder->coder);

	if (((recoder->rank == recoder->prev_rank) && (recoder->prev_rank== rank)) ||
			recoder->rank == recoder->symbols) {
		// No rank increase and the rank is same as the senders rank.
		// OR generation completed successfully. Send feedback.
		recoder->feedback_rank = recoder->rank;
		recoder->feedback_generation = recoder->generation;
		nck_trigger_call(&recoder->on_feedback_ready);
	}
	if (recoder->rank > recoder->prev_rank) {
		// Rank increase
		recoder->to_send += recoder->pace_redundancy;
		nck_trigger_call(&recoder->on_coded_ready);
		if (recoder->rank == recoder->symbols)
			_flush_coded(recoder);
	}

	if (timerisset(&recoder->rec_fb_timeout)){
		nck_timer_rearm(recoder->rec_fb_timeout_handle, &recoder->rec_fb_timeout);
	}

	if (timerisset(&recoder->rec_flush_timeout)){
		nck_timer_rearm(recoder->rec_flush_timeout_handle, &recoder->rec_flush_timeout);
	}

	if (nck_pace_rec_has_source(recoder)) {
		nck_trigger_call(&recoder->on_source_ready);
	}

	recoder->prev_rank = recoder->rank;

	return 0;
}

EXPORT
int nck_pace_rec_get_source(struct nck_pace_rec *recoder, struct sk_buff *packet)
{
    if (recoder->queue_length > recoder->queue_index) {
        skb_put(packet, recoder->source_size);
        memcpy(packet->data, recoder->queue + recoder->source_size * recoder->queue_index, recoder->source_size);
        recoder->queue_index += 1;
        return 0;
    }
	return kodo_get_source(recoder->coder, packet, recoder->buffer, &recoder->index, recoder->flush);
}

EXPORT
int nck_pace_rec_get_feedback(struct nck_pace_rec *recoder, struct sk_buff *packet)
{
	skb_reserve(packet, 6);
	skb_push_u16(packet, (uint16_t)recoder->feedback_rank);
	skb_push_u32(packet, recoder->feedback_generation);

	recoder->feedback_generation = 0;

	return 0;
}

EXPORT
int nck_pace_rec_has_feedback(struct nck_pace_rec *recoder)
{
	return recoder->feedback_generation != 0;
}

EXPORT
int nck_pace_rec_complete(struct nck_pace_rec *recoder)
{
	return (recoder->to_send < 100);
}


EXPORT
int nck_pace_rec_has_coded(struct nck_pace_rec *recoder)
{
	return recoder->to_send >= 100;
}


EXPORT
int nck_pace_rec_get_coded(struct nck_pace_rec *recoder, struct sk_buff *packet)
{
	if (!_has_coded(recoder)) {
		return -1;
	}

	skb_reserve(packet, 6);
	kodo_decoder_get_coded(recoder->coder, packet);
	skb_push_u16(packet, (uint16_t) recoder->rank);
	skb_push_u32(packet, recoder->generation);

	recoder->to_send -= 100;

	return 0;
}

EXPORT
int nck_pace_rec_put_feedback(struct nck_pace_rec *recoder, struct sk_buff *packet)
{
	uint32_t rank, generation;

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (generation > recoder->generation) {
		// Impossible state
		return  -1;
	}
	//fprintf(stderr, "\nRx feedback >> Gen: %d Rank: %d \tRecGen: %d RecRank: %d",
	//		generation, rank, recoder->generation, recoder->rank);

	if (generation == recoder->generation) {
		recoder->last_fb_rank = rank;
		if (rank == recoder->rank) {
			recoder->to_send = 0;
		}
	}

	return 0;
}
