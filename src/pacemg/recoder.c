#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include <nckernel/pacemg.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"
#include "list.h"

typedef struct nck_pacemg_rec_container {
	struct list_head list;
	struct nck_pacemg_rec *pacemg_recoder;
	krlnc_decoder_t coder;
	uint32_t generation;
	uint32_t rank;
	uint32_t prev_rank;
	uint8_t *buffer;
	int flush;
	uint32_t index;

	uint8_t *queue;
	int queue_index;
	int queue_length;

    	uint8_t *coded_queue, *coded_queue_addr;
    	int coded_queue_length;

	uint32_t to_send;
	uint32_t last_fb_rank;

	struct nck_timer_entry *rec_cont_flush_timeout_handle;

} rec_container;

struct nck_pacemg_rec {
	krlnc_decoder_factory_t factory;
	uint32_t gen_newest;
	uint32_t gen_oldest;
	uint32_t gen_oldest_deleted;
	uint32_t num_containers;
	uint32_t num_flushed_containers;
	rec_container *cont_oldest;
	rec_container *cont_newest;
	rec_container *cont_oldest_flushed;

	uint32_t block_size;
	uint32_t symbols;

	uint8_t *coded_pkts_per_input;

	uint16_t coding_ratio;
	uint16_t tail_packets;

	uint16_t max_active_containers;
	uint16_t max_containers;

	size_t source_size, coded_size, feedback_size;
	uint8_t to_send_rec;

	struct nck_timer *timer;                                // should be pointer?
	struct timeval rec_fb_timeout;
	struct nck_timer_entry *rec_fb_timeout_handle;
	struct timeval rec_flush_timeout;
	struct nck_timer_entry *rec_flush_timeout_handle;
	struct timeval rec_redundancy_timeout;
	struct nck_timer_entry *rec_redundancy_timeout_handle;

	struct nck_trigger on_source_ready, on_coded_ready, on_feedback_ready;
	struct list_head container_list;

};

NCK_RECODER_IMPL(nck_pacemg, nck_pacemg_rec_debug, NULL, NULL)

static void nck_pacemg_update_recoder_stat(struct nck_pacemg_rec *recoder) {
	if (!list_empty(&recoder->container_list)) {
		recoder->cont_newest = list_entry(recoder->container_list.prev, rec_container, list);
		recoder->gen_newest = recoder->cont_newest->generation;
		recoder->cont_oldest = list_entry(recoder->container_list.next, rec_container, list);
		recoder->gen_oldest = recoder->cont_oldest->generation;
	} else {
		recoder->cont_newest = NULL;
		recoder->cont_oldest = NULL;
	}

}

static void recoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	if(success){
		struct nck_pacemg_rec *recoder = (struct nck_pacemg_rec *) context;
		nck_pacemg_rec_flush_source(recoder);
	}
}
/*
static void recoder_send_feedback(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	UNUSED(context);
	UNUSED(success);
}
*/
static void rec_container_timeout_flush(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	if (success) {
		rec_container *container = (rec_container *) context;
		nck_pacemg_rec_flush_source(container->pacemg_recoder);
	}
}


EXPORT
rec_container *nck_pacemg_rec_start_next_generation(struct nck_pacemg_rec *recoder, uint32_t generation) {
	rec_container *container;
	rec_container *cont_tmp;
	container = malloc(sizeof(*container));
	memset(container, 0, sizeof(*container));

	container->pacemg_recoder = recoder;
	container->generation = generation;
	container->rank = 0;
	container->prev_rank = 0;
	container->flush = 0;
	container->index = 0;
	container->coder = kodo_build_decoder(recoder->factory);
	krlnc_decoder_set_status_updater_on(container->coder);

	container->queue = malloc(recoder->symbols * recoder->source_size);
	container->queue_index = 0;
	container->queue_length = 0;
	container->coded_queue_length = 0;
	nck_pacemg_reserve_rec_cont_coded_queue(container);

	container->to_send = 0;
	container->last_fb_rank = 0;

	container->buffer = malloc(recoder->block_size);
	memset(container->buffer, 0, recoder->block_size);
	krlnc_decoder_set_mutable_symbols(container->coder, container->buffer, recoder->block_size);

	container->rec_cont_flush_timeout_handle = nck_timer_add(recoder->timer, NULL, container,
								 rec_container_timeout_flush);

	INIT_LIST_HEAD(&container->list);

	// We ensure that the containers are placed in ascending order of the generation
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		if (cont_tmp->generation > generation) break;
	}
	list_add_before(&container->list, &cont_tmp->list);

	recoder->num_containers++;

	nck_pacemg_update_recoder_stat(recoder);

	return container;
}

/**
 * Gives debug info of the containers in the recoder
 * @param rec
 * @return
 */
EXPORT
char *nck_pacemg_rec_contdebug(void *rec) {
	static char debug[1024 * 1024], debug_head[128 * 128], result[2056 * 2056];
	struct nck_pacemg_rec *recoder = rec;

	strcpy(result, "\t\t\t\t");
	rec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &recoder->container_list, list) {
		snprintf(debug_head, sizeof(debug_head) - 1, "GEN IDX FLU RNK QID QLN CQL TSE\t\t");
		strcat(result, debug_head);
	}
	strcat(result,"\n\t\t\t\t");

	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &recoder->container_list, list) {
		snprintf(debug, sizeof(debug) - 1, "%*d %*d %*d %*d %*d %*d%*d%*d\t\t",
			 3, cont_tmp->generation, 3, cont_tmp->index, 3, cont_tmp->flush,
			 3, krlnc_decoder_rank(cont_tmp->coder),
			 3, cont_tmp->queue_index, 3, cont_tmp->queue_length, 3, cont_tmp->coded_queue_length,
			 3, cont_tmp->to_send
		);
		strcat(result, debug);
	}
	debug[sizeof(debug) - 1] = 0;
	strcat(result, "\n");

	return result;
}


EXPORT
char *nck_pacemg_rec_debug(void *rec) {
	static char debug[4096 * 10];
	struct nck_pacemg_rec *recoder = rec;
	int len = sizeof(debug), pos = 0;

	rec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &recoder->container_list, list) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"GEN\":%d,", cont_tmp->generation);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"IDX\":%d,", cont_tmp->index);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"FLU\":%d,", cont_tmp->flush);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"RNK\":%d,", krlnc_decoder_rank(cont_tmp->coder));
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"QID\":%d,", cont_tmp->queue_index);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"QLN\":%d,", cont_tmp->queue_length);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"CQL\":%d,", cont_tmp->coded_queue_length);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"TSE\":%d,", cont_tmp->to_send);
	}
	debug[sizeof(debug) - 1] = 0;

	return debug;
}


/*
static void recoder_send_redundancy(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);

	if (success) {
		struct nck_pacemg_rec *recoder = (struct nck_pacemg_rec *) context;
		rec_container *container = recoder->cont_oldest;

		if (container->rank > container->last_fb_rank) {
			container->to_send += recoder->coding_ratio / 100;
			recoder->to_send_rec += recoder->coding_ratio / 100;
			nck_trigger_call(&recoder->on_coded_ready);

			nck_timer_rearm(recoder->rec_fb_timeout_handle, &recoder->rec_fb_timeout);
//			fprintf(stderr, "\nSending redundancy");
		}

	}
}
 */

EXPORT
struct nck_pacemg_rec *nck_pacemg_rec(krlnc_decoder_factory_t factory, struct nck_timer *timer) {
	struct nck_pacemg_rec *result;
	uint32_t block_size;
	krlnc_decoder_t dec;

	dec = kodo_build_decoder(factory);
	block_size = krlnc_decoder_block_size(dec);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_coded_ready);
	nck_trigger_init(&result->on_feedback_ready);
	result->symbols = krlnc_decoder_factory_symbols(factory);

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(dec) + 6;
	result->feedback_size = 6;

	result->factory = factory;
	result->block_size = block_size;
	result->gen_oldest_deleted = 0;
	result->num_containers = 0;
	result->num_flushed_containers = 0;
	result->to_send_rec = 0;
	result->coded_pkts_per_input = malloc(sizeof(uint8_t) * result->symbols);
	memset(result->coded_pkts_per_input, 0, sizeof(uint8_t) * result->symbols);

	INIT_LIST_HEAD(&(result->container_list));

	result->timer = timer;

//	result->rec_fb_timeout_handle = nck_timer_add(timer, &result->rec_fb_timeout, result, recoder_send_feedback);
//	result->rec_redundancy_timeout_handle = nck_timer_add(timer, &result->rec_redundancy_timeout, result, recoder_send_redundancy);
//	result->rec_flush_timeout_handle = nck_timer_add(timer, &result->rec_flush_timeout, result, recoder_timeout_flush);

	krlnc_delete_decoder(dec);

	return result;
}


/**
 * Function to set the positions where the redundant packets should be placed in each generation.
 * @param encoder
 * @param coding_ratio the percentage of redundancy
 * @param tail_packets the packets that must be at the end
 *
 * @return 0 on sucess
 */
EXPORT
int nck_pacemg_set_rec_red_positions(struct nck_pacemg_rec *recoder, uint16_t coding_ratio, uint16_t tail_packets) {
	// First set the number of packets to be sent for each(every) input symbol to the coder
	for(uint32_t i = 0; i < recoder->symbols; i++) {
		recoder->coded_pkts_per_input[i] = (uint8_t)(coding_ratio / 100);
	}

	uint32_t num_redundant_pkts =  (recoder->symbols * (coding_ratio % 100) + 100 - 1) / 100;   // The +100-1 is to ceil

	if (tail_packets > num_redundant_pkts) {
		fprintf(stderr, "\nCoder config error : More tail packets than redundancies detected"
						"\nNum_Redundant:%d\tNum_Tail:%d"
						"\nSetting tail_packets to %d\n",
				num_redundant_pkts, tail_packets, num_redundant_pkts);
		tail_packets = (uint16_t) num_redundant_pkts;
	}
	int redundancies_to_pace = (num_redundant_pkts - tail_packets);

	if(redundancies_to_pace > 0){
		uint32_t subgen_sizes [redundancies_to_pace];
		for (uint32_t i = 0; i < recoder->symbols % redundancies_to_pace; i++) {
			subgen_sizes[i] = (recoder->symbols / redundancies_to_pace) + 1;
		}

		for (int i = recoder->symbols % redundancies_to_pace; i < redundancies_to_pace; i++) {
			subgen_sizes[i] = (recoder->symbols / redundancies_to_pace);
		}

		int last_paced = 0;
		for (int i = 0; i < redundancies_to_pace; i++) {
			recoder->coded_pkts_per_input[last_paced + subgen_sizes[i] - 1] += 1;
			last_paced = last_paced + subgen_sizes[i];
		}
	}
	recoder->coded_pkts_per_input[recoder->symbols - 1] += (uint8_t) tail_packets;

	for(uint32_t i = 0; i < recoder->symbols; i++)
		fprintf(stderr, "%*d", 3, recoder->coded_pkts_per_input[i]);
	fprintf(stderr, "\n");
	return 0;
}

EXPORT
void nck_pacemg_set_rec_fb_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_fb_timeout) {
	if (rec_fb_timeout != NULL) {
		recoder->rec_fb_timeout = *rec_fb_timeout;
	} else {
		timerclear(&recoder->rec_fb_timeout);
	};
}

EXPORT
void nck_pacemg_set_rec_flush_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_flush_timeout) {
	if (rec_flush_timeout != NULL) {
		recoder->rec_flush_timeout = *rec_flush_timeout;
	} else {
		timerclear(&recoder->rec_flush_timeout);
	}
}

EXPORT
void nck_pacemg_set_rec_redundancy_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_fb_timeout) {
	if (rec_fb_timeout != NULL) {
		recoder->rec_redundancy_timeout = *rec_fb_timeout;
	} else {
		timerclear(&recoder->rec_redundancy_timeout);
	}
}

EXPORT
void nck_pacemg_reserve_rec_cont_coded_queue(struct nck_pacemg_rec_container *container){
	container->coded_queue_addr = malloc((container->pacemg_recoder->coding_ratio/100 + 1) * container->pacemg_recoder->symbols * container->pacemg_recoder->coded_size);
	container->coded_queue = container->coded_queue_addr;
}

EXPORT
void nck_pacemg_set_rec_coding_ratio(struct nck_pacemg_rec *recoder, uint16_t coding_ratio) {
	recoder->coding_ratio = coding_ratio;
    nck_pacemg_set_rec_red_positions(recoder, recoder->coding_ratio, recoder->tail_packets);
}

EXPORT
void nck_pacemg_set_rec_tail_packets(struct nck_pacemg_rec *recoder, uint16_t tail_packets) {
	recoder->tail_packets = tail_packets;
    nck_pacemg_set_rec_red_positions(recoder, recoder->coding_ratio, recoder->tail_packets);
}

EXPORT
void nck_pacemg_set_rec_max_active_containers(struct nck_pacemg_rec *recoder, uint16_t max_active_containers) {
	recoder->max_active_containers = max_active_containers;
}

EXPORT
void nck_pacemg_set_rec_max_containers(struct nck_pacemg_rec *recoder, uint16_t max_containers) {
	recoder->max_containers = max_containers;
}

void nck_pacemg_rec_container_del(rec_container *container) {
	container->pacemg_recoder->to_send_rec -= container->to_send;
	list_del(&container->list);
	krlnc_delete_decoder(container->coder);
	nck_timer_cancel(container->rec_cont_flush_timeout_handle);
	nck_timer_free(container->rec_cont_flush_timeout_handle);
	free(container->buffer);
	free(container->queue);
	free(container->coded_queue_addr);
	free(container);
}


EXPORT
void nck_pacemg_rec_free(struct nck_pacemg_rec *recoder) {
	rec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &recoder->container_list, list) {
		nck_pacemg_rec_container_del(cont_tmp);
	}
	krlnc_delete_decoder_factory(recoder->factory);

	nck_timer_cancel(recoder->rec_fb_timeout_handle);
	nck_timer_free(recoder->rec_fb_timeout_handle);

	nck_timer_cancel(recoder->rec_redundancy_timeout_handle);
	nck_timer_free(recoder->rec_redundancy_timeout_handle);

	nck_timer_cancel(recoder->rec_flush_timeout_handle);
	nck_timer_free(recoder->rec_flush_timeout_handle);

	free(recoder->coded_pkts_per_input);

	free(recoder);
}

static int rec_cont_has_source(rec_container *container) {
	if (container->index == container->pacemg_recoder->symbols)
		return 2;

	if (container->queue_index < container->queue_length)
		return 1;

	if ((container->queue_index == container->queue_length) && (container->queue_index != 0))
		return 0;

	return krlnc_decoder_is_symbol_uncoded(container->coder, container->index);
}

EXPORT
int nck_pacemg_rec_has_source(struct nck_pacemg_rec *recoder) {
	// No containers are present
	if (list_empty(&recoder->container_list))
		return 0;

	rec_container *cont_tmp;

	// Skip to the container that has source pkts
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		if (rec_cont_has_source(cont_tmp) == 2)
			continue;
		if (cont_tmp->flush == 0)
			return rec_cont_has_source(cont_tmp);
		if (rec_cont_has_source(cont_tmp))
			return 1;
	}
	return 0;

}

EXPORT
int nck_pacemg_rec_full(struct nck_pacemg_rec *recoder) {
	UNUSED(recoder);
	// concept does not exist in multirecoder. recoder never full
	return 0;
}

/**
 * Flush a container and copy the decoded symbols from the coder into the container queue.
 *
 * WARNING this function does not free any memory including the coder or the buffer in the container
 * @param container - Container to flush
 * @param recoder
 */
int nck_pacemg_rec_flush_container(rec_container *container) {
	container->flush = 1;
	kodo_skip_undecoded(container->coder, &container->index);

	container->queue_index = 0;
	container->queue_length = kodo_flush_source(container->coder, container->buffer,
						    container->index, container->queue);

	//nck_trigger_set(&container->pacemg_recoder->on_source_ready,container, get_source_after_flush);

	if (container->queue_length > 0) {
		if (_has_source(container->pacemg_recoder)) {
			nck_trigger_call(&container->pacemg_recoder->on_source_ready);
		}
	}
	return container->queue_length;
}

EXPORT
void nck_pacemg_rec_flush_source(struct nck_pacemg_rec *recoder) {
	rec_container *cont_tmp;
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		nck_pacemg_rec_flush_container(cont_tmp);
	}
}

/**
 * Flush the oldest container and copy the decoded symbols from the coder into
 * the container queue.
 *
 * WARNING this function does not free any memory including the coder or the buffer in the container
 * @param recoder
 */
int nck_pacemg_rec_flush_oldest_container(struct nck_pacemg_rec *recoder) {
	rec_container *oldest_container = recoder->cont_oldest;
	if (!nck_pacemg_rec_flush_container(oldest_container))
		return 0;
	else
		return 1;
}

void nck_pacemg_rec_cont_flush_coded(rec_container *container){
	int sent_pace = 0;
	int redundancies_to_pace = (container->pacemg_recoder->symbols * (container->pacemg_recoder->coding_ratio%100))/100;
	for(uint8_t i = 0; i < container->rank; i++)
		sent_pace += container->pacemg_recoder->coded_pkts_per_input[i];
	container->to_send += (redundancies_to_pace * container->rank)/container->pacemg_recoder->symbols - sent_pace;
	container->pacemg_recoder->to_send_rec += (redundancies_to_pace * container->rank)/container->pacemg_recoder->symbols - sent_pace;
}

EXPORT
void nck_pacemg_rec_flush_coded(struct nck_pacemg_rec *recoder) {
	rec_container *cont_tmp;
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		nck_pacemg_rec_cont_flush_coded(cont_tmp);
	}
	nck_trigger_call(&recoder->on_coded_ready);
}

void nck_pacemg_rec_put_coded_to_queue(rec_container *container){
	struct sk_buff packet;
	uint8_t payload[container->pacemg_recoder->coded_size];
	memset(payload, 0, sizeof(uint8_t) * container->pacemg_recoder->coded_size);
	skb_new(&packet,payload,container->pacemg_recoder->coded_size);

	skb_reserve(&packet, 6);
	kodo_decoder_get_coded(container->coder, &packet);
	skb_push_u16(&packet, (uint16_t) container->rank);
	skb_push_u32(&packet, container->generation);

	memcpy(container->coded_queue + container->pacemg_recoder->coded_size * container->coded_queue_length,packet.data,container->pacemg_recoder->coded_size);
	container->coded_queue_length += 1;

	container->to_send -= 1;
	container->pacemg_recoder->to_send_rec -= 1;
}

EXPORT
int nck_pacemg_rec_put_coded(struct nck_pacemg_rec *recoder, struct sk_buff *packet) {
	uint32_t generation, rank;
	rec_container *cont_tmp, *cont = NULL, *oldest_unflushed_container = NULL, *cont_tmp2;

	if (packet->len < 6) {
		return ENOSPC;
	}

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);
	UNUSED(rank);

	// "cont" will contain the pointer to the container of the gen if exists else NULL
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		if (cont_tmp->generation != generation)
			continue;
		cont = cont_tmp;
		break;
	}

	if ((generation < recoder->gen_oldest) || (generation <= recoder->gen_oldest_deleted)) {
		// Encoder sending old gen packets. Should send feedback for each such packet??+
		// Right now, doing nothing.
		return 0;
	}
	if(!cont){
		if (recoder->gen_newest < generation ||
		    (recoder->gen_oldest_deleted < generation && generation < recoder->gen_newest)) {
			// Create new container and flush out oldest ones if necessary
			if (recoder->num_containers >= recoder->max_active_containers) {
				// "cont" will contain the pointer to the container
				list_for_each_entry(cont_tmp2, &recoder->container_list, list) {
					if (cont_tmp2->flush == 1)
						continue;
					oldest_unflushed_container = cont_tmp2;
					break;
				}
				if (!oldest_unflushed_container)
					oldest_unflushed_container = recoder->cont_oldest;
				if (nck_pacemg_rec_flush_container(oldest_unflushed_container) == 0) {
					recoder->gen_oldest_deleted = oldest_unflushed_container->generation;
					nck_pacemg_rec_container_del(oldest_unflushed_container);
					nck_pacemg_update_recoder_stat(recoder);
					recoder->num_containers--;
				} else {
					nck_pacemg_rec_cont_flush_coded(oldest_unflushed_container);
				}
			}

			if (recoder->num_containers >= recoder->max_containers) {
				recoder->gen_oldest_deleted = recoder->cont_oldest->generation;
				nck_pacemg_rec_container_del(recoder->cont_oldest);
				nck_pacemg_update_recoder_stat(recoder);
				recoder->num_containers--;
			}
			cont = nck_pacemg_rec_start_next_generation(recoder, generation);
			//printf("Containers: %d\n",recoder->num_containers);
		}
	}
	assert(cont != NULL);

	if (cont->flush == 1)
		// Trying to put_coded to an already flushed container
		return 0;

	if (cont->rank < recoder->symbols)
		kodo_put_coded(cont->coder, packet);

	cont->rank = krlnc_decoder_rank(cont->coder);

	if (((cont->rank == cont->prev_rank) && (cont->prev_rank == rank)) ||
	    cont->rank == recoder->symbols) {
		// No rank increase and the rank is same as the senders rank.
		// OR generation completed successfully. Send feedback.


		/* No feedback right now
		cont->feedback_rank = cont->rank;
		cont->feedback_generation = recoder->generation;
		nck_trigger_call(&recoder->on_feedback_ready);
		*/

	}
	if (cont->rank > cont->prev_rank) {
		// Rank increase
		cont->to_send += recoder->coding_ratio / 100;
		cont->to_send += recoder->coded_pkts_per_input[cont->rank - 1];
		recoder->to_send_rec += recoder->coding_ratio / 100;
		recoder->to_send_rec += recoder->coded_pkts_per_input[cont->rank - 1];

		nck_trigger_call(&recoder->on_coded_ready);
	}

	if (timerisset(&recoder->rec_flush_timeout) && recoder->rec_flush_timeout_handle)
		nck_timer_rearm(recoder->rec_flush_timeout_handle, &recoder->rec_flush_timeout);
	else if (timerisset(&recoder->rec_flush_timeout) && !recoder->rec_flush_timeout_handle)
		recoder->rec_flush_timeout_handle = nck_timer_add(recoder->timer, &recoder->rec_flush_timeout, recoder, recoder_timeout_flush);


	cont->prev_rank = cont->rank;
	// printf("Put coded: Gen %d Rank %d\n", cont->generation, cont->rank);
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
* @param recoder
* @param packet
* @return
*/
EXPORT
int nck_pacemg_rec_get_source(struct nck_pacemg_rec *recoder, struct sk_buff *packet) {

	if (!_has_source(recoder))
		return -1;

	rec_container *cont_tmp, *oldest_container = NULL;

	// Skip to the container that has source pkts
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		if (rec_cont_has_source(cont_tmp) == 2)
			continue;
		if (cont_tmp->flush == 0) {
			oldest_container = cont_tmp;
			break;
		}
		if (rec_cont_has_source(cont_tmp)) {
			oldest_container = cont_tmp;
			break;
		}
	}

	assert(oldest_container != NULL);

	if (oldest_container->queue_length > oldest_container->queue_index) {
		uint8_t *payload = skb_put(packet, (unsigned) recoder->source_size);
		memcpy(payload, oldest_container->queue + recoder->source_size * oldest_container->queue_index,
		       recoder->source_size);
		oldest_container->queue_index += 1;


		if ((oldest_container->queue_index == oldest_container->queue_length) &&
		    (oldest_container->to_send == 0)) {
			recoder->gen_oldest_deleted = recoder->cont_oldest->generation;
			nck_pacemg_rec_container_del(recoder->cont_oldest);
			nck_pacemg_update_recoder_stat(recoder);
			recoder->num_containers--;
		}
		return 0;
	}
	if (kodo_get_source(oldest_container->coder, packet, oldest_container->buffer, &oldest_container->index,
			    oldest_container->flush) < 0)
		return -1;

	if ((oldest_container->index == recoder->symbols) && (oldest_container->to_send == 0)) {
		recoder->gen_oldest_deleted = recoder->cont_oldest->generation;
		nck_pacemg_rec_container_del(recoder->cont_oldest);
		nck_pacemg_update_recoder_stat(recoder);
		recoder->num_containers--;
	}

	return 0;
}

EXPORT
int nck_pacemg_rec_get_feedback(struct nck_pacemg_rec *recoder, struct sk_buff *packet) {
	UNUSED(recoder);
	UNUSED(packet);
	return -1;
}

EXPORT
int nck_pacemg_rec_has_feedback(struct nck_pacemg_rec *recoder) {
	UNUSED(recoder);
	//Feedback not implemented yet..
	return 0;
}

EXPORT
int nck_pacemg_rec_has_coded(struct nck_pacemg_rec *recoder) {
	if (list_empty(&recoder->container_list))
		return 0;

	rec_container *cont_tmp;

	// Skip to the container that has source pkts
	list_for_each_entry(cont_tmp, &recoder->container_list, list) {
		if (nck_pacemg_rec_cont_has_coded(cont_tmp))
			return 1;
	}
	return recoder->to_send_rec >= 1;
}

EXPORT
int nck_pacemg_rec_cont_has_coded(rec_container *container) {
	if(container->coded_queue_length > 0)
		return 1;

	return container->to_send >= 1;
}

EXPORT
int nck_pacemg_rec_get_coded(struct nck_pacemg_rec *recoder, struct sk_buff *packet) {
	if (!_has_coded(recoder)) {
		return -1;
	}
	rec_container *container;
	// Skip to the container that has coded pkts
	list_for_each_entry(container, &recoder->container_list, list) {
		if (nck_pacemg_rec_cont_has_coded(container)) break;
	}

	if (container->coded_queue_length > 0) {
		uint8_t *payload = skb_put(packet, (unsigned) recoder->coded_size);
		memcpy(payload, container->coded_queue, recoder->coded_size);
		container->coded_queue_length -= 1;
		container->coded_queue = container->coded_queue + recoder->coded_size;

		while(container->to_send > 0){
			nck_pacemg_rec_put_coded_to_queue(container);
		}

		if ((container->coded_queue_length == 0) && (container->index == recoder->symbols)
			&& (container->to_send == 0)) {
			recoder->gen_oldest_deleted = container->generation;
			nck_pacemg_rec_container_del(container);
			nck_pacemg_update_recoder_stat(recoder);
			recoder->num_containers--;
		}

		return 0;
	}

	skb_reserve(packet, 6);
	kodo_decoder_get_coded(container->coder, packet);
	skb_push_u16(packet, (uint16_t) container->rank);
	skb_push_u32(packet, container->generation);

	container->to_send -= 1;
	recoder->to_send_rec -= 1;

	while(container->to_send > 0){
		nck_pacemg_rec_put_coded_to_queue(container);
	}

	if ((container->index == recoder->symbols) && (container->to_send == 0)) {
		recoder->gen_oldest_deleted = recoder->cont_oldest->generation;
		nck_pacemg_rec_container_del(recoder->cont_oldest);
		nck_pacemg_update_recoder_stat(recoder);
		recoder->num_containers--;
	}

	return 0;
}

EXPORT
int nck_pacemg_rec_put_feedback(struct nck_pacemg_rec *recoder, struct sk_buff *packet) {
	UNUSED(recoder);
	UNUSED(packet);
	/*
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
	*/
	return 0;
}

EXPORT
int nck_pacemg_rec_complete(struct nck_pacemg_rec *recoder) {
	UNUSED(recoder);
	return -1;
}
