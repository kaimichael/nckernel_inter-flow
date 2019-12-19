#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/pacemg.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"
#include "list.h"
#include "encdec.h"

typedef struct nck_pacemg_dec_container {
	struct list_head list;
	struct nck_pacemg_dec *pacemg_decoder;
	krlnc_decoder_t coder;
	uint32_t generation;
	uint16_t rank_enc_latest;
	uint16_t rank_dec;
	uint16_t seqno;
	uint8_t *buffer;
	int flush;
	uint32_t index;

	uint8_t *queue;
	int queue_index;
	int queue_length;

	struct nck_timer_entry *dec_cont_flush_timeout_handle;
} dec_container;

struct nck_pacemg_dec {
	krlnc_decoder_factory_t factory;
	uint32_t gen_newest;
	uint32_t gen_oldest;
	uint32_t gen_oldest_deleted;
	uint32_t block_size;
	uint32_t symbols;
	uint32_t num_containers;
	uint32_t oldest_global_seq;
	dec_container *cont_oldest;
	dec_container *cont_newest;

	uint16_t max_active_containers;
	size_t source_size, coded_size, feedback_size;
	struct nck_timer *timer;                                // should be pointer?

	struct nck_timer_entry *dec_fb_timeout_handle;

	struct timeval dec_flush_timeout;
	struct nck_timer_entry *dec_flush_timeout_handle;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	struct list_head container_list;

	int has_feedback;
};

NCK_DECODER_IMPL(nck_pacemg, nck_pacemg_dec_debug, NULL, NULL)

/**
 * updates the internal stats cont_newest, gen_newest, cont_oldest, gen_oldest
 * @param decoder pointer to the decoder to work on
 */
static void nck_pacemg_update_decoder_stat(struct nck_pacemg_dec *decoder) {
	if (!list_empty(&decoder->container_list)) {
		decoder->cont_newest = list_entry(decoder->container_list.prev, dec_container, list);
		decoder->gen_newest = decoder->cont_newest->generation;
		decoder->cont_oldest = list_entry(decoder->container_list.next, dec_container, list);
		decoder->gen_oldest = decoder->cont_oldest->generation;
#ifdef DEC_STATS
		fprintf(stderr, "decoder->gen_oldest == %d\n", decoder->gen_oldest);
#endif
	}
	else {
		decoder->cont_newest = NULL;
		decoder->cont_oldest = NULL;
	}

}

static void decoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	UNUSED(entry);
	if (success) {
		struct nck_pacemg_dec *decoder = (struct nck_pacemg_dec *) context;
		nck_pacemg_dec_flush_source(decoder);
	}
}

/*
static void decoder_send_feedback(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	UNUSED(context);
	UNUSED(success);
}
 */

static void dec_container_timeout_flush(struct nck_timer_entry *entry, void *context, int success) {
	UNUSED(entry);
	if (success) {
		dec_container *container = (dec_container *) context;
		nck_pacemg_dec_flush_container(container);
	}
}

dec_container *nck_pacemg_dec_start_next_generation(struct nck_pacemg_dec *decoder, uint32_t generation) {
	dec_container *container;
	dec_container *cont_tmp;
	container = malloc(sizeof(*container));
	memset(container, 0, sizeof(*container));

	container->pacemg_decoder = decoder;
	container->generation = generation;
	container->flush = 0;
	container->index = 0;
	container->coder = kodo_build_decoder(decoder->factory);
	krlnc_decoder_set_status_updater_on(container->coder);

	container->queue = malloc(decoder->symbols * decoder->source_size);
	container->queue_index = 0;
	container->queue_length = 0;


	container->buffer = malloc(decoder->block_size);
	memset(container->buffer, 0, decoder->block_size);
	krlnc_decoder_set_mutable_symbols(container->coder, container->buffer, decoder->block_size);

	container->dec_cont_flush_timeout_handle = nck_timer_add(decoder->timer, NULL, container,
															 dec_container_timeout_flush);

	INIT_LIST_HEAD(&container->list);

	// We ensure that the containers are placed in ascending order of the generation
	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		if (cont_tmp->generation > generation) {break;}
	}
	list_add_before(&container->list, &cont_tmp->list);

	decoder->num_containers++;

	nck_pacemg_update_decoder_stat(decoder);

	return container;
}

/**
 * Gives debug info of the containers in the decoder
 * @param rec
 * @return
 */
EXPORT
char *nck_pacemg_dec_contdebug(void *dec) {
	static char debug[1024], debug_head[128], result[2056];
	struct nck_pacemg_dec *decoder = dec;

	strcpy(result, "\t\t\t\t");
	dec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &decoder->container_list, list) {
		snprintf(debug_head, sizeof(debug_head) - 1, "GEN IDX FLU RNK QID QLN\t\t");
		strcat(result, debug_head);
	}
	strcat(result, "\n\t\t\t\t");

	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &decoder->container_list, list) {
		snprintf(debug, sizeof(debug) - 1, "%*d %*d %*d %*d %*d %*d\t\t",
				 3, cont_tmp->generation, 3, cont_tmp->index, 3, cont_tmp->flush,
				 3, krlnc_decoder_rank(cont_tmp->coder),
				 3, cont_tmp->queue_index, 3, cont_tmp->queue_length
		);
		strcat(result, debug);
	}
	debug[sizeof(debug) - 1] = 0;
	strcat(result, "\n");

	return result;
}


EXPORT
char *nck_pacemg_dec_debug(void *dec) {
	static char debug[4096 * 10];
	struct nck_pacemg_dec *decoder = dec;
	int len = sizeof(debug), pos = 0;

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"G_NEW\":%d,", decoder->gen_newest);
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"G_OLD\":%d,", decoder->gen_oldest);
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"G_DEL\":%d,", decoder->gen_oldest_deleted);
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"#CONT\":%d,", decoder->num_containers);

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"CONT\": {");
	dec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &decoder->container_list, list) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"GEN\":%d,", cont_tmp->generation);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"IDX\":%d,", cont_tmp->index);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"FLU\":%d,", cont_tmp->flush);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"RNK\":%d,", krlnc_decoder_rank(cont_tmp->coder));
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"QID\":%d,", cont_tmp->queue_index);
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"QLN\":%d,", cont_tmp->queue_length);
	}
	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "}");

	debug[sizeof(debug) - 1] = 0;

	return debug;
}


EXPORT
struct nck_pacemg_dec *nck_pacemg_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer) {
	struct nck_pacemg_dec *result;
	uint32_t block_size;
	krlnc_decoder_t dec;

	dec = kodo_build_decoder(factory);
	block_size = krlnc_decoder_block_size(dec);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);

	result->symbols = krlnc_decoder_factory_symbols(factory);

	result->has_feedback = 0;

	result->source_size = krlnc_decoder_factory_symbol_size(factory);
	result->coded_size = krlnc_decoder_payload_size(dec) + nck_pacemg_pkt_header_size;
	result->feedback_size = nck_pacemg_pkt_feedback_additional
							+ nck_pacemg_pkt_feedback_size_single * MAX_MAX_ACTIVE_CONTAINERS;

	result->factory = factory;
	result->block_size = block_size;
	result->gen_oldest_deleted = 0;

	INIT_LIST_HEAD(&(result->container_list));

	nck_pacemg_dec_start_next_generation(result, 1);

	result->timer = timer;

//	result->dec_fb_timeout_handle = nck_timer_add(timer, NULL, result, decoder_send_feedback);
//	result->dec_flush_timeout_handle = nck_timer_add(timer, NULL, result, decoder_timeout_flush);

	krlnc_delete_decoder(dec);

	return result;
}

EXPORT
void nck_pacemg_set_dec_fb_timeout(struct nck_pacemg_dec *decoder, const struct timeval *dec_fb_timeout) {
	UNUSED(dec_fb_timeout);
	UNUSED(decoder);
}

EXPORT
void nck_pacemg_set_dec_max_active_containers(struct nck_pacemg_dec *decoder, uint16_t max_active_containers) {
	decoder->max_active_containers = max_active_containers;
	decoder->feedback_size =
			nck_pacemg_pkt_feedback_size_single * max_active_containers + nck_pacemg_pkt_feedback_additional;
}

EXPORT
void nck_pacemg_set_dec_flush_timeout(struct nck_pacemg_dec *decoder, const struct timeval *dec_flush_timeout) {
	if (dec_flush_timeout != NULL) {
		decoder->dec_flush_timeout = *dec_flush_timeout;
	}
	else {
		timerclear(&decoder->dec_flush_timeout);
	}
}

void nck_pacemg_dec_container_del(dec_container *container) {
	list_del(&container->list);
	krlnc_delete_decoder(container->coder);
	nck_timer_cancel(container->dec_cont_flush_timeout_handle);
	nck_timer_free(container->dec_cont_flush_timeout_handle);
	free(container->buffer);
	free(container->queue);
	free(container);
}

EXPORT
void nck_pacemg_dec_free(struct nck_pacemg_dec *decoder) {
	dec_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &decoder->container_list, list) {
		nck_pacemg_dec_container_del(cont_tmp);
	}

	krlnc_delete_decoder_factory(decoder->factory);

	nck_timer_cancel(decoder->dec_fb_timeout_handle);
	nck_timer_free(decoder->dec_fb_timeout_handle);

	nck_timer_cancel(decoder->dec_flush_timeout_handle);
	nck_timer_free(decoder->dec_flush_timeout_handle);

	free(decoder);
}

EXPORT
int nck_pacemg_dec_has_source(struct nck_pacemg_dec *decoder) {
	dec_container *oldest_container = decoder->cont_oldest;

	// No containers are present
	if (list_empty(&decoder->container_list)) {
#ifdef DEC_HAS_SOURCE
		fprintf(stderr, "nck_pacemg_dec_has_source %d 0\n", __LINE__);
#endif
		return 0;
	}
	if ((oldest_container->queue_index == oldest_container->queue_length) && (oldest_container->queue_index != 0)) {
#ifdef DEC_HAS_SOURCE
		fprintf(stderr, "nck_pacemg_dec_has_source %d 0\n", __LINE__);
#endif
		return 0;
	}
	if (oldest_container->index == decoder->symbols) {
#ifdef DEC_HAS_SOURCE
		fprintf(stderr, "nck_pacemg_dec_has_source %d 0\n", __LINE__);
#endif
		return 0;
	}
	if (oldest_container->queue_index < oldest_container->queue_length) {
#ifdef DEC_HAS_SOURCE
		fprintf(stderr, "nck_pacemg_dec_has_source %d 1\n", __LINE__);
#endif
		return 1;
	}
	if (decoder->gen_oldest > decoder->gen_oldest_deleted + 1) {
#ifdef DEC_HAS_SOURCE
		fprintf(stderr, "nck_pacemg_dec_has_source %d 0\n", __LINE__);
#endif
		return 0;
	}
	uint8_t ret = krlnc_decoder_is_symbol_uncoded(oldest_container->coder, oldest_container->index);
#ifdef DEC_HAS_SOURCE
	fprintf(stderr, "nck_pacemg_dec_has_source %d kodo: %d\n", __LINE__, ret);
#endif
	return ret;
}

EXPORT
int nck_pacemg_dec_full(struct nck_pacemg_dec *decoder) {
	UNUSED(decoder);
	// concept does not exist in multidecoder. Decoder never full
	return 0;
}

/**
 * Flush a container and copy the decoded symbols from the coder into the container queue.
 *
 * WARNING this function does not free any memory including the coder or the buffer in the container
 * @param container - Container to flush
 *
 * @returns amount of undecoded symbols in container
 */
int nck_pacemg_dec_flush_container(dec_container *container) {
	container->flush = 1;
	kodo_skip_undecoded(container->coder, &container->index);

	container->queue_index = 0;
	container->queue_length = kodo_flush_source(container->coder, container->buffer,
												container->index, container->queue);
	if (container->queue_length > 0) {
		if (_has_source(container->pacemg_decoder)) {
			nck_trigger_call(&container->pacemg_decoder->on_source_ready);
		}
	}
	return container->queue_length;
}

/**
 * Flush all containers of the decoder. It copies all the decoded symbols from the coder to the container queue
 *
 * WARNING this function does not free any memory including the coder or the buffer in the container
 *
 * @param decoder
 */
EXPORT
void nck_pacemg_dec_flush_source(struct nck_pacemg_dec *decoder) {
	dec_container *cont_tmp;
	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		nck_pacemg_dec_flush_container(cont_tmp);
	}
}

/**
 * Flush the oldest container and copy the decoded symbols from the coder into
 * the container queue.
 *
 * WARNING this function does not free any memory including the coder or the buffer in the container
 * @param decoder
 */
void nck_pacemg_dec_flush_oldest_container(struct nck_pacemg_dec *decoder) {
	dec_container *oldest_container = decoder->cont_oldest;
	nck_pacemg_dec_flush_container(oldest_container);
}

void nck_pacemg_dec_add_feedback(struct nck_pacemg_dec *decoder) {
	decoder->has_feedback = 1;
}

EXPORT
int nck_pacemg_dec_put_coded(struct nck_pacemg_dec *decoder, struct sk_buff *packet) {
	dec_container *cont_tmp, *cont = NULL;

	if (packet->len < nck_pacemg_pkt_header_size) {
		return ENOSPC;
	}

	struct nck_pacemg_pkt_header header = {
			.generation = skb_pull_u32(packet),
			.rank          = skb_pull_u16(packet),
			.seqno         = skb_pull_u16(packet),
			.global_seqno  = skb_pull_u32(packet),
			.feedback_flag = skb_pull_u8(packet)
	};

#ifdef DEC_PACKETS_CODED
	static uint32_t j = 0;
	fprintf(stderr, ANSI_COLOR_GREEN "DEC COD " ANSI_COLOR_MAGENTA "%2d %3d %2d %4d:" ANSI_COLOR_GREEN , header.generation, header.seqno, header.rank, header.global_seqno);
	for(uint8_t *i = packet->data; i < packet->tail; i++)
	{
		fprintf(stderr, " %02X", *i);
	}
	fprintf(stderr, ANSI_COLOR_RESET " %d\n", j++);
#endif

	// "cont" will contain the pointer to the container of the gen if exists else NULL
	list_for_each_entry(cont_tmp, &decoder->container_list, list) {
		if (cont_tmp->generation == header.generation) {
			cont = cont_tmp;
			break;
		}
	}

	if ((header.generation < decoder->gen_oldest) || (header.generation <= decoder->gen_oldest_deleted)) {
		return 0;
	}
	// if the packet belongs to a new generation
	if (!cont) {
		// Create new conatainer and flush out oldest ones if necessary
		if (decoder->num_containers >= decoder->max_active_containers) {
			if (nck_pacemg_dec_flush_container(decoder->cont_oldest) == 0) {
				decoder->gen_oldest_deleted = decoder->cont_oldest->generation;
				nck_pacemg_dec_container_del(decoder->cont_oldest);
				nck_pacemg_update_decoder_stat(decoder);
				decoder->num_containers--;
			}
		}
		cont = nck_pacemg_dec_start_next_generation(decoder, header.generation);
	}
	assert(cont != NULL);

	kodo_put_coded(cont->coder, packet);

	//update seqno and rank
	cont->seqno = header.seqno;
	cont->rank_dec = krlnc_decoder_rank(cont->coder);
	cont->rank_enc_latest = header.rank;
	decoder->oldest_global_seq = header.global_seqno;
#ifdef DEC_RANK
	fprintf(stderr, "%3d " ANSI_COLOR_GREEN "%2d " ANSI_COLOR_RED "%2d\n" ANSI_COLOR_RESET "", cont->generation, cont->rank, header.rank);
#endif


	if (timerisset(&decoder->dec_flush_timeout) && decoder->dec_flush_timeout_handle) {
		nck_timer_rearm(decoder->dec_flush_timeout_handle, &decoder->dec_flush_timeout);
	}
	else if (timerisset(&decoder->dec_flush_timeout) && !decoder->dec_flush_timeout_handle) {
		decoder->dec_flush_timeout_handle = nck_timer_add(decoder->timer, &decoder->dec_flush_timeout, decoder,
														  decoder_timeout_flush);
	}

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	if (header.feedback_flag) {
		nck_pacemg_dec_add_feedback(decoder);
	}

	if (_has_feedback(decoder)) {
		nck_trigger_call(&decoder->on_feedback_ready);
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
int nck_pacemg_dec_get_source(struct nck_pacemg_dec *decoder, struct sk_buff *packet) {
	dec_container *oldest_container = decoder->cont_oldest;

	if (oldest_container->queue_length > oldest_container->queue_index) {
		uint8_t *payload = skb_put(packet, (unsigned) decoder->source_size);
		memcpy(payload, oldest_container->queue + decoder->source_size * oldest_container->queue_index++,
			   decoder->source_size);

#ifdef DEC_PACKETS_SOURCE
		static uint16_t j = 0;
		fprintf(stderr, ANSI_COLOR_BLUE "DEC SRC " ANSI_COLOR_MAGENTA "====> %3d %4d:" ANSI_COLOR_GREEN "", oldest_container->generation, j++);
		for(uint8_t *i = packet->data; i < packet->tail; i++)
		{
			fprintf(stderr, " %02X", *i);
		}
		fprintf(stderr, ANSI_COLOR_RESET "\n");
#endif

		if (oldest_container->queue_index == oldest_container->queue_length) {
			decoder->gen_oldest_deleted = oldest_container->generation;
			nck_pacemg_dec_container_del(oldest_container);
			nck_pacemg_update_decoder_stat(decoder);
			decoder->num_containers--;
		}

		return 0;
	}
	if (kodo_get_source(oldest_container->coder, packet, oldest_container->buffer, &oldest_container->index,
						oldest_container->flush) < 0) {
		return -1;
	}
#ifdef DEC_PACKETS_SOURCE
	static uint16_t j = 0;
	fprintf(stderr, ANSI_COLOR_BLUE "DEC SRC " ANSI_COLOR_MAGENTA "====> %3d %4d:" ANSI_COLOR_GREEN "", oldest_container->generation, j++);
	for(uint8_t *i = packet->data; i < packet->tail; i++)
	{
		fprintf(stderr, " %02X", *i);
	}
	fprintf(stderr, ANSI_COLOR_RESET "\n");
#endif
	if (oldest_container->index == decoder->symbols) {
		decoder->gen_oldest_deleted = oldest_container->generation;
		nck_pacemg_dec_container_del(oldest_container);
		nck_pacemg_update_decoder_stat(decoder);
		decoder->num_containers--;
	}
	return 0;
}

EXPORT
int nck_pacemg_dec_get_feedback(struct nck_pacemg_dec *decoder, struct sk_buff *packet) {
	struct nck_pacemg_dec_container *container;

	decoder->has_feedback = 0;

#ifdef DEC_PACKETS_FEEDBACK
	fprintf(stderr, ANSI_COLOR_GREEN "DEC  FB " ANSI_COLOR_YELLOW "----> %3d %4d: " ANSI_COLOR_GREEN, decoder->gen_oldest, decoder->oldest_global_seq);
#endif
	assert(skb_tailroom(packet) >= 4 + 4);
	skb_put_u32(packet, decoder->gen_oldest);
	skb_put_u32(packet, decoder->oldest_global_seq);

	//put feedback for each active container in packet
	list_for_each_entry(container, &decoder->container_list, list) {
		assert(skb_tailroom(packet) >= nck_pacemg_pkt_feedback_size_single);
#ifdef DEC_PACKETS_FEEDBACK
		fprintf(stderr, "%2d %2d %2d      ", container->generation, container->rank_enc_latest, container->rank_dec);
#endif
		skb_put_u32(packet, container->generation);
		// Put the "latest encoder rank" in the feedback packet if its the newest generation. Otherwise, make it
		// equal to the full rank. This helps especially when the last x packets of a generation is lost
		if (container == decoder->cont_newest) {
			skb_put_u16(packet, container->rank_enc_latest);

		} else {
			skb_put_u16(packet, (uint16_t) decoder->symbols);
		}
		skb_put_u16(packet, container->rank_dec);
		skb_put_u16(packet, container->seqno);
	}

#ifdef DEC_PACKETS_FEEDBACK
	fprintf(stderr, ANSI_COLOR_RESET "\n");
#endif

	return 0;
}

EXPORT
int nck_pacemg_dec_has_feedback(struct nck_pacemg_dec *decoder) {
	int ret = decoder->has_feedback;
#ifdef DEC_HAS_FEEDBACK
	fprintf(stderr, "nck_pacemg_dec_has_feedback %d\n", ret);
#endif
	return ret;
}

EXPORT
int nck_pacemg_dec_complete(struct nck_pacemg_dec *decoder) {
	UNUSED(decoder);
	return -1;
}
