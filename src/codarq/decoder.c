#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/codarq.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"
#include "list.h"

typedef struct nck_codarq_dec_container {
	struct list_head list;
	struct nck_codarq_dec *codarq_decoder;
	krlnc_decoder_t coder;
	uint32_t generation;
	uint32_t rank;
	uint32_t enc_rank;
	uint8_t *buffer;
	uint32_t index;

	struct nck_timer_entry *dec_cont_flush_timeout_handle;

} dec_container;

struct nck_codarq_dec {
	krlnc_decoder_factory_t factory;
	uint32_t last_seqno;
	uint32_t gen_newest;
	uint32_t gen_oldest;
	uint32_t gen_oldest_deleted;
	uint32_t block_size;
	uint32_t symbols;
	uint32_t num_containers;
	uint8_t has_feedback;
	dec_container *cont_oldest;
	dec_container *cont_newest;

	size_t source_size, coded_size, feedback_size;
	struct nck_timer *timer;                                // should be pointer?

	struct timeval dec_fb_timeout;
	struct nck_timer_entry *dec_fb_timeout_handle;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	struct list_head container_list;
};

NCK_DECODER_IMPL(nck_codarq, nck_codarq_dec_debug, NULL, NULL)

static void nck_codarq_update_decoder_stat(struct nck_codarq_dec *decoder) {
	if (!list_empty(&decoder->container_list)) {
		decoder->cont_newest = list_entry(decoder->container_list.prev, dec_container, list);
		decoder->gen_newest = decoder->cont_newest->generation;
		decoder->cont_oldest = list_entry(decoder->container_list.next, dec_container, list);
		decoder->gen_oldest = decoder->cont_oldest->generation;
	} else {
		decoder->cont_newest = NULL;
		decoder->cont_oldest = NULL;
	}

}

static void decoder_send_feedback(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);

	if (success) {
		struct nck_codarq_dec *decoder = (struct nck_codarq_dec *) context;
		decoder->has_feedback = 1;
		nck_trigger_call(&decoder->on_feedback_ready);

		// Send feedback if any active containers are present and didn't recieve any coded pkts for over a time period.
		if (timerisset(&decoder->dec_fb_timeout)) {
			if (decoder->num_containers > 0)
				nck_timer_rearm(decoder->dec_fb_timeout_handle, &decoder->dec_fb_timeout);
		}
	}
}

dec_container *nck_codarq_dec_start_next_generation(struct nck_codarq_dec *decoder, uint32_t generation) {
	dec_container *container;
	dec_container *cont_tmp;
	container = malloc(sizeof(*container));
	memset(container, 0, sizeof(*container));

	if (decoder->cont_newest) {
		// if we start the next generation then the previous generation should be full
		decoder->cont_newest->enc_rank = decoder->symbols;
	}

	container->codarq_decoder = decoder;
	container->generation = generation;
	container->rank = 0;
	container->enc_rank = decoder->symbols;
	container->index = 0;
	container->coder = kodo_build_decoder(decoder->factory);
	krlnc_decoder_set_status_updater_on(container->coder);

	container->buffer = malloc(decoder->block_size);
	memset(container->buffer, 0, decoder->block_size);
	krlnc_decoder_set_mutable_symbols(container->coder, container->buffer, decoder->block_size);

	decoder->num_containers += 1;

	INIT_LIST_HEAD(&container->list);

	// We ensure that the containers are placed in ascending order of the generation
	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		if (cont_tmp->generation > generation) break;
	}
	list_add_before(&container->list, &cont_tmp->list);

	nck_codarq_update_decoder_stat(decoder);

	return container;
}

EXPORT
char *nck_codarq_dec_debug(void *dec) {
	static char debug[40000];
	struct nck_codarq_dec *decoder = dec;
	char *sep = "";
	int len = sizeof(debug), pos = 0;
	dec_container *cont;

	debug[0] = 0;

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
		 "\"newest generation\": %d, \"has source\": %d, \"num\": %u, \"last_seqno\": %u, \"containers\": [",
		 decoder->gen_newest, _has_source(decoder), decoder->num_containers, decoder->last_seqno);

	list_for_each_entry(cont, &decoder->container_list, list) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
			 "%s{ \"gen\": %u, \"rank\": %u }",
			 sep, cont->generation, cont->rank);
		sep = ", ";
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
		 "]");

	debug[pos] = 0;

	return debug;
}


EXPORT
struct nck_codarq_dec *nck_codarq_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer) {
	struct nck_codarq_dec *result;
	uint32_t block_size;
	krlnc_decoder_t dec;

	dec = kodo_build_decoder(factory);
	block_size = krlnc_decoder_block_size(dec);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->symbols = krlnc_decoder_factory_symbols(factory);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(dec) + 4 + 2 + 4;
	result->feedback_size = 1408;        // Supports up to 140 containers

	result->last_seqno = 0;
	result->factory = factory;
	result->block_size = block_size;
	result->gen_oldest_deleted = 0;
	result->has_feedback = 0;


	INIT_LIST_HEAD(&(result->container_list));
	nck_codarq_dec_start_next_generation(result, 1);

	result->timer = timer;

	result->dec_fb_timeout_handle = nck_timer_add(timer, NULL, result, decoder_send_feedback);

	krlnc_delete_decoder(dec);

	return result;
}

EXPORT
void nck_codarq_set_dec_fb_timeout(struct nck_codarq_dec *decoder, const struct timeval *dec_fb_timeout) {
	if (dec_fb_timeout != NULL) {
		decoder->dec_fb_timeout= *dec_fb_timeout;
	} else {
		timerclear(&decoder->dec_fb_timeout);
	}
}

void nck_codarq_dec_container_del(dec_container *container) {
	container->codarq_decoder->num_containers -= 1;
	list_del(&container->list);
	krlnc_delete_decoder(container->coder);
	nck_timer_cancel(container->dec_cont_flush_timeout_handle);
	nck_timer_free(container->dec_cont_flush_timeout_handle);
	free(container->buffer);
	free(container);
}

EXPORT
void nck_codarq_dec_free(struct nck_codarq_dec *decoder) {
	dec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &decoder->container_list, list) {
		nck_codarq_dec_container_del(cont_tmp);
	}
	krlnc_delete_decoder_factory(decoder->factory);

	nck_timer_cancel(decoder->dec_fb_timeout_handle);
	nck_timer_free(decoder->dec_fb_timeout_handle);

	free(decoder);
}

EXPORT
int nck_codarq_dec_has_source(struct nck_codarq_dec *decoder) {
	dec_container *oldest_container = decoder->cont_oldest;

	// No containers are present
	if (list_empty(&decoder->container_list))
		return 0;

	// do not skip over a missing generation if no packets haven't been received yet
	// TODO: this is not wrap-around safe
	if (decoder->gen_oldest > decoder->gen_oldest_deleted + 1)
		return 0;

	return krlnc_decoder_is_symbol_uncoded(oldest_container->coder, oldest_container->index);
}

EXPORT
int nck_codarq_dec_full(struct nck_codarq_dec *decoder) {
	UNUSED(decoder);
	// concept does not exist in multidecoder. Decoder never full
	return 0;
}

/**
 * The concept of FLUSH is irrelevant in CODARQ
 * @param decoder
 */
EXPORT
void nck_codarq_dec_flush_source(struct nck_codarq_dec *decoder) {
	UNUSED(decoder);
}

EXPORT
int nck_codarq_dec_put_coded(struct nck_codarq_dec *decoder, struct sk_buff *packet) {
	uint32_t generation, rank, seqno;
	dec_container *cont_tmp, *cont = NULL;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);
	seqno = skb_pull_u32(packet);

	if ((int32_t)(decoder->last_seqno - seqno) < 0) {
		decoder->last_seqno = seqno;
	}

	// "cont" will contain the pointer to the container of the gen if exists else NULL
	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		if (cont_tmp->generation != generation)
			continue;
		cont = cont_tmp;
		break;
	}

	if ((int32_t)(generation - decoder->gen_oldest_deleted) <= 0) {
		// Encoder sending old gen packets.
		//
		// This prevents that the encoder hangs when it did miss a final feedback.
		//
		// This feedback is sent on every packet, may want to change for bandwidth
		// optimization.
		decoder->has_feedback = 1;
		nck_trigger_call(&decoder->on_feedback_ready);
		return 0;
	}

	if (!cont) {
		while (decoder->gen_newest < generation ||
		    (decoder->gen_oldest_deleted < generation && generation < decoder->gen_newest)) {
			// Create new container
			cont = nck_codarq_dec_start_next_generation(decoder, decoder->gen_newest+1);
		}
	}

	assert(cont != NULL);

	cont->enc_rank = rank;

	if (cont->rank < decoder->symbols)
		kodo_put_coded(cont->coder, packet);

	cont->rank = krlnc_decoder_rank(cont->coder);

	// printf("\n Put Coded - Cont: %d \t Rank: %d ", cont->generation, cont->rank);

	// we are currently sending one feedback per pkt received. Must change that for BW optimization
	decoder->has_feedback = 1;
	nck_trigger_call(&decoder->on_feedback_ready);

	// Send feedback if any active containers are present and didn't recieve any coded pkts for over a time period.
	if (timerisset(&decoder->dec_fb_timeout)) {
		if (decoder->num_containers > 0)
			nck_timer_rearm(decoder->dec_fb_timeout_handle, &decoder->dec_fb_timeout);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	return 0;
}

/**
 * Copies the decoded symbols from the queue of the oldest container. If the queue is empty, it gets the
 * decoded pkts from the coder buffer.
 *
 * In either cases, if all the "possibly decodable" packets are copied out, it deletes the container.
 *
 * WARNING this assumes that the _has_source() has been called and returned positive, prior to this call
 *
 * @param decoder
 * @param packet
 * @return
 */
EXPORT
int nck_codarq_dec_get_source(struct nck_codarq_dec *decoder, struct sk_buff *packet) {

	if (!_has_source(decoder))
		return -1;

	dec_container *oldest_container = decoder->cont_oldest;

	if (kodo_get_source(oldest_container->coder, packet, oldest_container->buffer,
			    &oldest_container->index, 0) < 0)
		return -1;
	if (oldest_container->index == decoder->symbols) {
		decoder->gen_oldest_deleted = oldest_container->generation;
		nck_codarq_dec_container_del(oldest_container);
		nck_codarq_update_decoder_stat(decoder);
	}
	return 0;
}

EXPORT
int nck_codarq_dec_get_feedback(struct nck_codarq_dec *decoder, struct sk_buff *packet) {
	dec_container *cont_tmp;

	skb_put_u32(packet, decoder->gen_oldest_deleted);
	skb_put_u32(packet, decoder->num_containers);
	skb_put_u32(packet, decoder->last_seqno);

	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		skb_put_u32(packet, cont_tmp->generation);
		skb_put_u16(packet, (uint16_t)(cont_tmp->enc_rank - cont_tmp->rank));
	}

	decoder->has_feedback = 0;

	return 0;
}

EXPORT
int nck_codarq_dec_has_feedback(struct nck_codarq_dec *decoder) {
	return decoder->has_feedback;
}

EXPORT
int nck_codarq_dec_complete(struct nck_codarq_dec *decoder) {
	UNUSED(decoder);
	return -1;
}
