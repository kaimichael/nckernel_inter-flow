#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/codarq.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"
#include "list.h"


typedef struct nck_codarq_enc_container {
	struct list_head list;
	struct nck_codarq_enc *codarq_encoder;
	krlnc_encoder_t coder;
	uint32_t generation;
	uint32_t rank;
	uint32_t last_seqno;
	uint32_t to_send_cont;
	uint8_t *buffer;
} enc_container;

struct nck_codarq_enc {
	krlnc_encoder_factory_t factory;

	uint32_t block_size;
	uint32_t symbols;

	uint32_t redundancy;

	uint32_t seqno;
	uint32_t gen_newest;
	uint32_t gen_oldest;
	uint32_t num_containers;
	uint8_t max_active_containers;
	enc_container *cont_oldest;
	enc_container *cont_newest;

	struct nck_timer *timer;
	struct timeval enc_repair_timeout;
	struct nck_timer_entry *repair_timeout_handle;
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_coded_ready;
	int to_send;
	struct list_head container_list;
};

EXPORT
char *nck_codarq_enc_debug(void *enc) {
	static char debug[40000];
	struct nck_codarq_enc *encoder = enc;
	char *sep = "";
	int len = sizeof(debug), pos = 0;
	enc_container *cont;

	debug[0] = 0;

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
		 "\"newest generation\": %d, \"to_send\": %d, \"num\": %u, \"containers\": [",
		 encoder->gen_newest, encoder->to_send, encoder->num_containers);

	list_for_each_entry(cont, &encoder->container_list, list) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
			 "%s{ \"gen\": %u, \"rank\": %u, \"last_seqno\": %u, \"to_send\": %u}",
			 sep, cont->generation, cont->rank, cont->last_seqno, cont->to_send_cont);
		sep = ", ";
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos),
		 "]");

	debug[pos] = 0;

	return debug;
}

NCK_ENCODER_IMPL(nck_codarq, nck_codarq_enc_debug, NULL, NULL)

static void nck_codarq_update_encoder_stat(struct nck_codarq_enc *encoder) {
	if (!list_empty(&encoder->container_list)) {
		encoder->cont_newest = list_entry(encoder->container_list.prev, enc_container, list);
		encoder->gen_newest = encoder->cont_newest->generation;
		encoder->cont_oldest = list_entry(encoder->container_list.next, enc_container, list);
		encoder->gen_oldest = encoder->cont_oldest->generation;
	} else {
		encoder->cont_newest = NULL;
		encoder->cont_oldest = NULL;
	}
}

static void enc_repair_timeout(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	if (success) {
		// send a packet from the oldest generation
		struct nck_codarq_enc *encoder = (struct nck_codarq_enc*)context;
		enc_container *container = encoder->cont_oldest;

		if (container != NULL) {
			encoder->to_send += 1;
			container->to_send_cont += 1;

			nck_timer_rearm(encoder->repair_timeout_handle, &encoder->enc_repair_timeout);
			nck_trigger_call(&container->codarq_encoder->on_coded_ready);
		}
	}
}


enc_container *nck_codarq_enc_start_next_generation(struct nck_codarq_enc *encoder, uint32_t generation) {
	enc_container *container;
	enc_container *cont_tmp;
	container = malloc(sizeof(*container));
	memset(container, 0, sizeof(*container));

	container->codarq_encoder = encoder;
	container->generation = generation;
	container->rank = 0;
	container->last_seqno = 0;
	container->to_send_cont = 0;
	container->coder = kodo_build_encoder(encoder->factory);

	container->buffer = malloc(encoder->block_size);
	memset(container->buffer, 0, encoder->block_size);

	encoder->num_containers += 1;

	INIT_LIST_HEAD(&container->list);

	// We ensure that the containers are placed in ascending order of the generation
	list_for_each_entry(cont_tmp, &encoder->container_list, list) {
		if (cont_tmp->generation > generation) break;
	}
	list_add_before(&container->list, &cont_tmp->list);

	nck_codarq_update_encoder_stat(encoder);

	return container;
}

EXPORT
struct nck_codarq_enc *nck_codarq_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer) {
	struct nck_codarq_enc *result;
	uint32_t block_size;
	krlnc_encoder_t enc;

	enc = kodo_build_encoder(factory);
	block_size = krlnc_encoder_block_size(enc);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_coded_ready);

	result->symbols = krlnc_encoder_factory_symbols(factory);
	result->factory = factory;
	result->block_size = block_size;

	result->seqno = 0;
	result->to_send = 0;
	result->gen_oldest = 0;
	result->gen_newest = 0;

	if (timer) {
		result->repair_timeout_handle = nck_timer_add(timer, NULL, result, enc_repair_timeout);
	} else {
		result->repair_timeout_handle = NULL;
	}

	INIT_LIST_HEAD(&(result->container_list));

	result->source_size = krlnc_encoder_factory_symbol_size(factory);
	result->coded_size = krlnc_encoder_payload_size(enc) + 4 + 2 + 4;
	result->feedback_size = 1408;
	result->timer = timer;

	krlnc_delete_encoder(enc);

	return result;
}

EXPORT
void nck_codarq_set_redundancy(struct nck_codarq_enc *encoder, uint16_t redundancy) {
	encoder->redundancy = redundancy;
}

EXPORT
void nck_codarq_set_enc_max_active_containers(struct nck_codarq_enc *encoder, uint8_t max_active_containers) {
	encoder->max_active_containers = max_active_containers;
}

EXPORT
void nck_codarq_set_enc_repair_timeout(struct nck_codarq_enc *encoder, const struct timeval *enc_repair_timeout) {
	if (enc_repair_timeout != NULL) {
		encoder->enc_repair_timeout = *enc_repair_timeout;
	} else {
		timerclear(&encoder->enc_repair_timeout);
	}
}


void nck_codarq_enc_container_del(enc_container *container) {
	container->codarq_encoder->num_containers -= 1;
	list_del(&container->list);
	krlnc_delete_encoder(container->coder);
	free(container->buffer);
	free(container);
}

EXPORT
void nck_codarq_enc_free(struct nck_codarq_enc *encoder) {
	enc_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &encoder->container_list, list) {
		nck_codarq_enc_container_del(cont_tmp);
	}
	krlnc_delete_encoder_factory(encoder->factory);
	free(encoder);
}

EXPORT
int nck_codarq_enc_has_coded(struct nck_codarq_enc *encoder) {
	return encoder->to_send >= 1;
}

EXPORT
int nck_codarq_enc_cont_has_coded(struct nck_codarq_enc_container *container) {
	return container->to_send_cont >= 1;
}

EXPORT
int nck_codarq_enc_full(struct nck_codarq_enc *encoder) {
	return encoder->num_containers >= encoder->max_active_containers &&
	       (!encoder->cont_newest || encoder->cont_newest->rank == encoder->symbols);
}

EXPORT
int nck_codarq_enc_complete(struct nck_codarq_enc *encoder) {
	// Whatever this means..
	UNUSED(encoder);
	return 0;
}

EXPORT
void nck_codarq_enc_flush_coded(struct nck_codarq_enc *encoder) {
	// Flush has no meaning for us yet.
	UNUSED(encoder);
	// nck_trigger_call(&encoder->on_coded_ready);

}

EXPORT
int nck_codarq_enc_get_coded(struct nck_codarq_enc *encoder, struct sk_buff *packet) {
	enc_container *container;
	int found = 0;

	if (!_has_coded(encoder)) {
		return -1;
	}

	// Skip to the container that has coded pkts
	list_for_each_entry(container, &encoder->container_list, list) {
		if (nck_codarq_enc_cont_has_coded(container)) {
			found = 1;
			break;
		}
	}

	assert(found);
	if (!found)
		return -1;

	encoder->seqno += 1;

	skb_reserve(packet, 4 + 2 + 4);        // gen, rank, seq
	kodo_encoder_get_coded(container->coder, packet);

	skb_push_u32(packet, encoder->seqno);
	skb_push_u16(packet, (uint16_t) container->rank);
	skb_push_u32(packet, container->generation);

	encoder->to_send 	-= 1;
	container->to_send_cont -= 1;
	container->last_seqno = encoder->seqno;

	if (encoder->repair_timeout_handle) {
		if (_has_coded(encoder)) {
			nck_timer_cancel(encoder->repair_timeout_handle);
		} else {
			nck_timer_rearm(encoder->repair_timeout_handle, &encoder->enc_repair_timeout);
		}
	}

	return 0;
}

EXPORT
int nck_codarq_enc_put_source(struct nck_codarq_enc *encoder, struct sk_buff *packet) {
	enc_container *container;

	if (nck_codarq_enc_full(encoder)) {
		fprintf(stderr, "\nNCK_ERROR: Put_Source: Encoder is full.");
		return -1;
	}

	if (list_empty(&encoder->container_list) || encoder->cont_newest->rank == encoder->symbols) {
		nck_codarq_enc_start_next_generation(encoder, encoder->gen_newest + 1);
	}

	container = encoder->cont_newest;

	kodo_put_source(container->coder, packet, container->buffer, container->rank);

	container->rank = krlnc_encoder_rank(container->coder);

	container->to_send_cont += 1;
	encoder->to_send += 1;

	if (container->rank == encoder->symbols) {
		container->to_send_cont += encoder->redundancy;
		encoder->to_send += encoder->redundancy;
	}

	nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}

/**
 * Put feedback into the container and generate necessary coded packets.
 * NOTE: This function assumes that all coded packets will be sent at the tail.
 * DO NOT RESUSE FOR PACE
 *
 * @param container
 * @param rank - rank received from feedback
 * @param seqno - seqno received from feedback
 */
static void enc_cont_put_feedback(enc_container *container, uint16_t rank_diff, uint32_t seqno) {
	int32_t seqdiff = container->last_seqno - seqno;
	if (seqdiff > 0) {
		// feedback is not recent enough
		return;
	}

	if (container->to_send_cont > 0) {
		// this should not happen if: BDP > generation size
		// if it does happen we just wait until we drained the planned transmission pool
		return;
	}

	container->to_send_cont = rank_diff;
	container->codarq_encoder->to_send += rank_diff;
}

EXPORT
int nck_codarq_enc_put_feedback(struct nck_codarq_enc *encoder, struct sk_buff *packet) {
	enc_container *cont_tmp, *cont_tmp_safe;
	uint32_t decoded_gen, num_dec_containers, rx_gen, rx_seq;
	uint16_t rx_rank_diff;

	decoded_gen = skb_pull_u32(packet);
	num_dec_containers = skb_pull_u32(packet);
	rx_seq = skb_pull_u32(packet);

	// delete all generations which are not necessary anymore
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &encoder->container_list, list) {
		if (cont_tmp->generation <= decoded_gen) {
			encoder->to_send -= cont_tmp->to_send_cont;
			cont_tmp->to_send_cont = 0;
			nck_codarq_enc_container_del(cont_tmp);
		}
	}

	nck_codarq_update_encoder_stat(encoder);

	// process feedback per generation
	for (uint32_t i = 0; i < num_dec_containers; i++) {
		rx_gen = skb_pull_u32(packet);
		rx_rank_diff = skb_pull_u16(packet);

		list_for_each_entry(cont_tmp, &encoder->container_list, list) {
			if (cont_tmp->generation == rx_gen) {
				enc_cont_put_feedback(cont_tmp, rx_rank_diff, rx_seq);
				break;
			}
		}
	}

	if (_has_coded(encoder)) {
		nck_trigger_call(&encoder->on_coded_ready);
	}

	if (encoder->repair_timeout_handle) {
		if (_has_coded(encoder)) {
			nck_timer_cancel(encoder->repair_timeout_handle);
		} else {
			nck_timer_rearm(encoder->repair_timeout_handle, &encoder->enc_repair_timeout);
		}
	}

	return 0;
}



