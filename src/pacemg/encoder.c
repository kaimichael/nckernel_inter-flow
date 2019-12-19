#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/pacemg.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"
#include "list.h"
#include "encdec.h"

typedef struct nck_pacemg_enc_container {
	struct list_head list;
	struct nck_pacemg_enc *pacemg_encoder;
	krlnc_encoder_t coder;
	uint32_t generation;
	uint16_t rank;
	uint32_t to_send;
	uint32_t sent;
	uint8_t *buffer;
	uint32_t *coded_pkts_seq_nos;
	uint32_t coded_pkt_seq_nos_index;
} enc_container;

struct nck_pacemg_enc {
	krlnc_encoder_factory_t factory;

	uint32_t block_size;
	uint32_t symbols;
	uint32_t packets;

	uint8_t feedback;

	uint16_t coding_ratio;
	uint16_t tail_packets;

	uint32_t gen_newest;
	uint32_t gen_oldest;
	uint32_t num_containers;
	uint32_t max_active_containers;
	uint32_t global_seqno;
	uint32_t max_cont_coded_history;
	enc_container *cont_oldest;
	enc_container *cont_newest;

	struct nck_timer *timer;
	struct timeval enc_redundancy_timeout, enc_flush_timeout;
	struct nck_timer_entry *enc_redundancy_timeout_handle, *enc_flush_timeout_handle;

	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_coded_ready;

	uint8_t *coded_pkts_per_input;
	uint8_t *coded_pkts_so_far;
	uint8_t *rank_per_pkt;
	struct list_head container_list;
};

NCK_ENCODER_IMPL(nck_pacemg, NULL, NULL, NULL)

/**
 * updates the internal stats cont_newest, gen_newest, cont_oldest, gen_oldest
 * @param encoder pointer to the encoder to work on
 */
static void nck_pacemg_enc_update_stat(struct nck_pacemg_enc *encoder) {
	if (!list_empty(&encoder->container_list)) {
		encoder->cont_newest = list_entry(encoder->container_list.prev, enc_container, list);
		encoder->gen_newest = encoder->cont_newest->generation;
		encoder->cont_oldest = list_entry(encoder->container_list.next, enc_container, list);
		encoder->gen_oldest = encoder->cont_oldest->generation;
	}
	else {
		encoder->cont_newest = NULL;
		encoder->cont_oldest = NULL;
	}
}

/**
 * deletes a container and removes it from the encoders queue
 * @param container pointer to the container to work on
 */
void nck_pacemg_enc_container_del(enc_container *container) {
#ifdef ENC_ADD_DEL_CONTAINER
	fprintf(stderr, "\nnck_pacemg_enc_container_del: gen == %d sent == %d\n", container->generation, container->sent);
#endif
	list_del(&container->list);

	container->pacemg_encoder->num_containers -= 1;
	nck_pacemg_enc_update_stat(container->pacemg_encoder);

	krlnc_delete_encoder(container->coder);
	free(container->coded_pkts_seq_nos);
	free(container->buffer);
	free(container);
}

/**
 * adds a container to the encoders queue
 * @param encoder pointer to the encoder to work on
 * @param container pointer to the container to add
 */
void nck_pacemg_enc_container_add(struct nck_pacemg_enc *encoder, enc_container *container) {
#ifdef ENC_ADD_DEL_CONTAINER
	fprintf(stderr  , "nck_pacemg_enc_add_container: gen == %d\n", container->generation);
#endif
	enc_container *cont_tmp;
	list_for_each_entry(cont_tmp, &encoder->container_list, list) {
		if (cont_tmp->generation > container->generation) {break;}
	}
	list_add_before(&container->list, &cont_tmp->list);
	nck_pacemg_enc_update_stat(encoder);
}


/**
 * starts a new generation and adds a new container for that generation to the queue
 * @param encoder pointer to the encoder to work on
 * @param generation number of the new generation
 *
 * @return the newly created container
 */
enc_container *nck_pacemg_enc_start_next_generation(struct nck_pacemg_enc *encoder) {
	enc_container *container;
	container = malloc(sizeof(*container));
	memset(container, 0, sizeof(*container));

	container->pacemg_encoder = encoder;
	container->generation = encoder->gen_newest + 1;
	container->rank = 0;
	container->to_send = 0;
	container->sent = 0;
	container->coder = kodo_build_encoder(encoder->factory);

	container->buffer = malloc(encoder->block_size);
	memset(container->buffer, 0, encoder->block_size);

	container->coded_pkts_seq_nos = malloc(encoder->max_cont_coded_history * sizeof(uint32_t));		// Number of seq nos to remember TODO: Make it configurable
	memset(container->coded_pkts_seq_nos, 0, encoder->max_cont_coded_history * sizeof(uint32_t));
	container->coded_pkt_seq_nos_index = 0;

	encoder->num_containers += 1;

	INIT_LIST_HEAD(&container->list);

	// We ensure that the containers are placed in ascending order of the generation
	nck_pacemg_enc_container_add(encoder, container);

	return container;
}

/**
 * Constructor
 * @param factory factory to generate encoder from
 * @param timer timer for timeouts
 *
 * @return pointer to the new encoder
 */
EXPORT
struct nck_pacemg_enc *nck_pacemg_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer) {
	struct nck_pacemg_enc *result;
	krlnc_encoder_t enc;

	enc = kodo_build_encoder(factory);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_coded_ready);

	result->factory = factory;
	result->symbols = krlnc_encoder_factory_symbols(factory);
	result->block_size = krlnc_encoder_block_size(enc);
	result->coding_ratio = 100; //default value
	result->tail_packets = 0;   //default value

	result->num_containers = 0;
	result->max_active_containers = 1;
	result->feedback = 1;

//TODO error: ‘enc_redundancy_timeout’ undeclared
	result->timer = timer;
//	if (timer) {
//		result->enc_redundancy_timeout_handle = nck_timer_add(timer, NULL, result, enc_redundancy_timeout);
//		result->enc_flush_timeout_handle      = nck_timer_add(timer, NULL, result, enc_flush_timeout);
//	} else {
//		result->enc_redundancy_timeout_handle = NULL;
//		result->enc_flush_timeout_handle      = NULL;
//	}

	INIT_LIST_HEAD(&(result->container_list));

	result->coded_pkts_per_input = malloc(sizeof(uint8_t) * result->symbols);
	memset(result->coded_pkts_per_input, 0, sizeof(uint8_t) * result->symbols);
	result->coded_pkts_so_far = NULL;
	result->rank_per_pkt = NULL;

	result->gen_newest = 0;
	result->gen_oldest = 0;
	result->global_seqno = 0;

	result->source_size = krlnc_encoder_factory_symbol_size(factory);
	result->coded_size = krlnc_encoder_payload_size(enc) + nck_pacemg_pkt_header_size;
	result->feedback_size = nck_pacemg_pkt_feedback_additional
							+ nck_pacemg_pkt_feedback_size_single * MAX_MAX_ACTIVE_CONTAINERS;

	krlnc_delete_encoder(enc);

	return result;
}

/**
 * currently empty
 * @param encoder pointer to the encoder to work on
 */
void nck_pacemg_reserve_enc_coded_queue(struct nck_pacemg_enc *encoder) {
	UNUSED(encoder);
}

/**
 * sets the maximum number of active containers
 * @param encoder pointer to the encoder to work on
 * @param max_active_containers new maximum number of active containers
 */
EXPORT
void nck_pacemg_set_enc_max_active_containers(struct nck_pacemg_enc *encoder, uint32_t max_active_containers) {
	encoder->max_active_containers = max_active_containers;
	encoder->feedback_size =
			nck_pacemg_pkt_feedback_size_single * max_active_containers + nck_pacemg_pkt_feedback_additional;
}

/**
 * sets the coding ratio in percent
 * 100 means no coded packets
 * @param encoder pointer to the encoder to work on
 * @param coding_ratio new coding ratio
 */
EXPORT
void nck_pacemg_set_enc_coding_ratio(struct nck_pacemg_enc *encoder, uint16_t coding_ratio) {
	encoder->coding_ratio = coding_ratio;
	nck_pacemg_set_enc_red_positions(encoder, encoder->coding_ratio, encoder->tail_packets);
}

/**
 * sets the number of additional tail packets
 * one more tail packet will be created if teh coding ratio is > 100
 * @param encoder pointer to the encoder to work on
 * @param tail_packets new number of additional tail packets
 */
EXPORT
void nck_pacemg_set_enc_tail_packets(struct nck_pacemg_enc *encoder, uint16_t tail_packets) {
	encoder->tail_packets = tail_packets;
	nck_pacemg_set_enc_red_positions(encoder, encoder->coding_ratio, encoder->tail_packets);
}

/**
 * sets the feedback on or off
 * @param encoder pointer to the encoder to work on
 * @param feedback boolean value if the feedback should be turned on
 */
EXPORT
void nck_pacemg_set_feedback(struct nck_pacemg_enc *encoder, uint8_t feedback) {
	encoder->feedback = feedback;
}

/**
 * sets the maximum number of the seqnos of sent coded packets that may be remembered by the encoder.
 * A large number will imply that the encoder will be very "Optimistic" regarding the packets in flight, i.e.
 * it won't send any extra packets if there is already some packets in flight that could potentially fix the loss
 * @param encoder pointer to the encoder to work on
 * @param history_size size of the history
 */
EXPORT
void nck_pacemg_set_enc_max_cont_coded_history(struct nck_pacemg_enc *encoder, uint32_t history_size) {
	encoder->max_cont_coded_history = history_size;
}

/**
 * Function to set the positions where the redundant packets should be placed in each generation.
 * @param encoder
 * @param coding_ratio the percentage of redundancy (not correct for values over 200%)
 * @param tail_packets the packets that must be at the end
 *
 * @return 0 on success
 */
EXPORT
int nck_pacemg_set_enc_red_positions(struct nck_pacemg_enc *encoder, uint16_t coding_ratio, uint16_t tail_packets) {
	// First set the number of packets to be sent for each(every) input symbol to the coder
	for (uint32_t i = 0; i < encoder->symbols; i++) {
		encoder->coded_pkts_per_input[i] = (uint8_t) (coding_ratio / 100);
	}

	uint32_t num_redundant_pkts = (encoder->symbols * (coding_ratio % 100) + 100 - 1) / 100;   // The +100-1 is to ceil
	encoder->packets = num_redundant_pkts + encoder->symbols * (uint8_t) (coding_ratio / 100);

	if (tail_packets > num_redundant_pkts) {
		fprintf(stderr, "\nCoder config error : More tail packets than redundancies detected"
						"\nNum_Redundant:%d\tNum_Tail:%d"
						"\nSetting tail_packets to %d\n",
				num_redundant_pkts, tail_packets, num_redundant_pkts);
		tail_packets = (uint16_t) num_redundant_pkts;
	}
	int redundancies_to_pace = (num_redundant_pkts - tail_packets);

	if (redundancies_to_pace > 0) {
		uint32_t subgen_sizes[redundancies_to_pace];
		for (uint32_t i = 0; i < encoder->symbols % redundancies_to_pace; i++) {
			subgen_sizes[i] = (encoder->symbols / redundancies_to_pace) + 1;
		}

		for (int i = encoder->symbols % redundancies_to_pace; i < redundancies_to_pace; i++) {
			subgen_sizes[i] = (encoder->symbols / redundancies_to_pace);
		}

		int last_paced = 0;
		for (int i = 0; i < redundancies_to_pace; i++) {
			encoder->coded_pkts_per_input[last_paced + subgen_sizes[i] - 1] += 1;
			last_paced += subgen_sizes[i];
		}
	}
	encoder->coded_pkts_per_input[encoder->symbols - 1] += (uint8_t) tail_packets;

	free(encoder->rank_per_pkt);
	encoder->rank_per_pkt = malloc(sizeof(uint8_t) * encoder->packets);
	memset(encoder->rank_per_pkt, 0, sizeof(uint8_t) * encoder->packets);
	for (uint32_t i = 0, j = 0; i < encoder->symbols; i++) {
		encoder->rank_per_pkt[j] = 1;
		j += encoder->coded_pkts_per_input[i];
	}

	//calculate number of previous coded packets for every packet
	free(encoder->coded_pkts_so_far);
	encoder->coded_pkts_so_far = malloc(sizeof(uint8_t) * encoder->packets);
	uint8_t coded_pkts_so_far = 0;
	for (uint32_t j = 0; j < encoder->packets; j++) {
		if (encoder->rank_per_pkt[j] == 0) {
			coded_pkts_so_far += 1;
		}
		encoder->coded_pkts_so_far[j] = coded_pkts_so_far;
	}

//	for (uint32_t i = 0; i < encoder->symbols; i++) {
//		fprintf(stderr, ANSI_COLOR_RED "%*d", 3, encoder->coded_pkts_per_input[i]);
//	}
//	fprintf(stderr, ANSI_COLOR_RESET "\n");
//	for (uint32_t i = 0; i < encoder->packets; i++) {
//		fprintf(stderr, ANSI_COLOR_YELLOW "%*d", 3, encoder->rank_per_pkt[i]);
//	}
//	fprintf(stderr, "\x1b[39m\n");
//	for (uint32_t i = 0; i < encoder->packets; i++) {
//		fprintf(stderr, ANSI_COLOR_GREEN "%*d", 3, encoder->coded_pkts_so_far[i]);
//	}
//	fprintf(stderr, ANSI_COLOR_RESET "\n");
	return 0;
}

/**
 * sets the duration of the feedback timeout
 * @param encoder pointer to the encoder to work on
 * @param enc_fb_timeout new value for the timeout
 */
EXPORT
void nck_pacemg_set_enc_redundancy_timeout(struct nck_pacemg_enc *encoder, const struct timeval *enc_fb_timeout) {
	if (enc_fb_timeout != NULL) {
		encoder->enc_redundancy_timeout = *enc_fb_timeout;
	}
	else {
		timerclear(&encoder->enc_redundancy_timeout);
	}
}

/**
 * sets the duration of the flush timeout
 * @param encoder pointer to the encoder to work on
 * @param enc_flush_timeout new value for the timeout
 */
EXPORT
void nck_pacemg_set_enc_flush_timeout(struct nck_pacemg_enc *encoder, const struct timeval *enc_flush_timeout) {
	if (enc_flush_timeout != NULL) {
		encoder->enc_flush_timeout = *enc_flush_timeout;
	}
	else {
		timerclear(&encoder->enc_flush_timeout);
	}
}

/**
 * deletes the encoder
 * @param encoder pointer to the encoder to work on
 */
EXPORT
void nck_pacemg_enc_free(struct nck_pacemg_enc *encoder) {
	enc_container *cont_tmp, *cont_tmp_safe;
	list_for_each_entry_safe(cont_tmp, cont_tmp_safe, &encoder->container_list, list) {
		nck_pacemg_enc_container_del(cont_tmp);
	}

	nck_timer_cancel(encoder->enc_redundancy_timeout_handle);
	nck_timer_free(encoder->enc_redundancy_timeout_handle);

	nck_timer_cancel(encoder->enc_flush_timeout_handle);
	nck_timer_free(encoder->enc_flush_timeout_handle);

	krlnc_delete_encoder_factory(encoder->factory);

	free(encoder->coded_pkts_per_input);
	free(encoder->coded_pkts_so_far);
	free(encoder->rank_per_pkt);

	free(encoder);
}

/**
 * checks if the container has coded packets available
 * @param container pointer to the container to work on
 *
 * @return 1 if the container has coded packets available
 */
int nck_pacemg_enc_cont_has_coded(struct nck_pacemg_enc_container *container) {
	return container->to_send >= 1;
}

/**
 * checks if the encoder has coded packets available
 * @param encoder pointer to the encoder to work on
 *
 * @return 1 if the encoder has coded packets available
 */
EXPORT
int nck_pacemg_enc_has_coded(struct nck_pacemg_enc *encoder) {
	enc_container *container;
	list_for_each_entry(container, &encoder->container_list, list) {
		if (nck_pacemg_enc_cont_has_coded(container)) {
			return 1;
		}
	}
	return 0;
}

/**
 * checks if the encoder's queue is full
 * currently just calls nck_pacemg_enc_has_coded to guarantee correct order of systematic and coded packets
 * @param encoder pointer to the encoder to work on
 *
 * @return 1 if the encoder's queue is full
 */
EXPORT
int nck_pacemg_enc_full(struct nck_pacemg_enc *encoder) {
	return nck_pacemg_enc_has_coded(encoder) ||
		   (encoder->num_containers >= encoder->max_active_containers && encoder->cont_newest->rank == encoder->symbols);
}

/**
 * checks if the encoder's queue is empty
 * @param encoder pointer to the encoder to work on
 *
 * @return 1 if the encoder's queue is empty
 */
EXPORT
int nck_pacemg_enc_complete(struct nck_pacemg_enc *encoder) {
	return encoder->num_containers == 0;
}

/**
 * currently empty
 * @param encoder pointer to the encoder to work on
 */
EXPORT
void nck_pacemg_enc_flush_coded(struct nck_pacemg_enc *encoder) {
//TODO
//	int sent_pace = 0;
//	int redundancies_to_pace = (encoder->symbols * (encoder->coding_ratio%100))/100;
//	for(uint8_t i = 0; i < encoder->rank; i++)
//		sent_pace += encoder->coded_pkts_per_input[i];
//	encoder->to_send += (redundancies_to_pace * encoder->rank)/encoder->symbols - sent_pace;
	UNUSED(encoder);
}

/**
 * checks if feedback should be requested
 * @param encoder pointer to the encoder to work on
 *
 * @return 1 if feedback should be requested
 */
EXPORT
uint8_t nck_pacemg_enc_calculate_feedback(struct nck_pacemg_enc *encoder) {
	return encoder->feedback;	//FIXME temporarily solution, requesting feedback on only a few packets to save bandwidth on the feedback channel
}

/**
 * gets coded packets from the encoder
 * @param encoder pointer to the encoder to work on
 * @param packet buffer for the packets
 *
 * @return 0 on success
 */
EXPORT
int nck_pacemg_enc_get_coded(struct nck_pacemg_enc *encoder, struct sk_buff *packet) {
	enc_container *container;
	int found = 0;
	int coded = 0;

	if (!_has_coded(encoder)) {
		return -1;
	}

	// Skip to the container that has coded pkts
	list_for_each_entry(container, &encoder->container_list, list) {
		if (nck_pacemg_enc_cont_has_coded(container)) {
			found = 1;
			break;
		}
	}

	assert(found);

	skb_reserve(packet, nck_pacemg_pkt_header_size);

	kodo_encoder_get_coded(container->coder, packet);
	if (packet->data[4] != 0xff) {
		coded = 1;
	}

	struct nck_pacemg_pkt_header header = {
			.generation    = container->generation,
			.rank          = container->rank,
			.seqno         = container->sent,
			.global_seqno  = encoder->global_seqno,
			.feedback_flag = nck_pacemg_enc_calculate_feedback(encoder)
	};

#ifdef ENC_PACKETS_CODED
	static uint32_t j = 0;
	fprintf(stderr, ANSI_COLOR_RED "ENC COD " ANSI_COLOR_BLUE "%2d %3d %2d %4d:" ANSI_COLOR_RED,
			header.generation, header.seqno, header.rank, header.global_seqno);
	for(uint8_t *i = packet->data; i < packet->tail; i++)
	{
		fprintf(stderr, " %02X", *i);
	}
	fprintf(stderr, ANSI_COLOR_RESET " %d\n", j++);
#endif

	skb_push_u8(packet, header.feedback_flag);
	skb_push_u32(packet, header.global_seqno);
	skb_push_u16(packet, header.seqno);
	skb_push_u16(packet, header.rank);
	skb_push_u32(packet, header.generation);

	// Remember the sequence nos of the last x coded packets sent by the container. Using a poor man's ring buffer.
	if (coded) {
		container->coded_pkts_seq_nos[container->coded_pkt_seq_nos_index] = encoder->global_seqno;
		if (container->coded_pkt_seq_nos_index < encoder->max_cont_coded_history - 1) {
			container->coded_pkt_seq_nos_index += 1;
		} else {
			container->coded_pkt_seq_nos_index = 0;
		}
	}

	container->to_send -= 1;
	container->sent += 1;
	encoder->global_seqno += 1;

    if(encoder->num_containers >= encoder->max_active_containers &&
       encoder->cont_newest->rank == encoder->symbols &&
       encoder->cont_oldest->rank == encoder->symbols &&
       encoder->cont_oldest->to_send == 0) {
#ifdef ENC_CONTAINER_DELETED
        fprintf(stderr, "container deleted gen: %d\n", encoder->cont_oldest->generation);
#endif
        nck_pacemg_enc_container_del(encoder->cont_oldest);
    }
	return 0;
}

/**
 * puts source packets into the encoder
 * @param encoder pointer to the encoder to work on
 * @param packet buffer with the packets
 *
 * @return 0 on success
 */
EXPORT
int nck_pacemg_enc_put_source(struct nck_pacemg_enc *encoder, struct sk_buff *packet) {
	if (nck_pacemg_enc_full(encoder)) {
		fprintf(stderr, "\nNCK_ERROR: Put_Source: Encoder is full.");
		return -1;
	}

	if (list_empty(&encoder->container_list) || encoder->cont_newest->rank == encoder->symbols) {
		nck_pacemg_enc_start_next_generation(encoder);
	}

	enc_container *container = encoder->cont_newest;

#ifdef ENC_PACKETS_SOURCE
	static uint16_t j = 0;
	fprintf(stderr, ANSI_COLOR_CYAN "ENC SRC " ANSI_COLOR_MAGENTA "%2d       %5d:" ANSI_COLOR_RED, container->generation, j++);
	for(uint8_t *i = packet->data; i < packet->tail; i++)
	{
		fprintf(stderr, " %02X", *i);
	}
	fprintf(stderr, ANSI_COLOR_RESET "\n");
#endif

	kodo_put_source(container->coder, packet, container->buffer, container->rank);

	container->rank = krlnc_encoder_rank(container->coder);

	container->to_send += encoder->coded_pkts_per_input[container->rank - 1];

	if (timerisset(&encoder->enc_flush_timeout)) {
		nck_timer_rearm(encoder->enc_flush_timeout_handle, &encoder->enc_flush_timeout);
	}

	if (timerisset(&encoder->enc_redundancy_timeout)) {
		nck_timer_rearm(encoder->enc_redundancy_timeout_handle, &encoder->enc_redundancy_timeout);
	}

	nck_trigger_call(&encoder->on_coded_ready);
	if(encoder->num_containers >= encoder->max_active_containers &&
       encoder->cont_newest->rank == encoder->symbols &&
	   encoder->cont_oldest->rank == encoder->symbols &&
	   encoder->cont_oldest->to_send == 0) {
#ifdef ENC_CONTAINER_DELETED
		fprintf(stderr, "container deleted gen: %d\n", encoder->cont_oldest->generation);
#endif
		nck_pacemg_enc_container_del(encoder->cont_oldest);
	}
	return 0;
}

/**
 * puts feedback packets into the encoder
 * @param container pointer to the container to work on
 * @param seqno sequence number which to checkFIXME
 *
 * @return number of already sent coded packets after the given sequence number
 */
uint32_t nck_pacemg_enc_additional_coded(enc_container *container, uint32_t seqno) {
	uint32_t already_sent_coded_packets;
	assert(container->sent > 0);
	// The FEC phase is ongoing
	if (seqno < container->pacemg_encoder->packets) {
		if (container->sent <= container->pacemg_encoder->packets) {
			already_sent_coded_packets = container->pacemg_encoder->coded_pkts_so_far[container->sent - 1];
		}
		else {
			already_sent_coded_packets = container->pacemg_encoder->packets - container->pacemg_encoder->symbols;
		}
		already_sent_coded_packets -= container->pacemg_encoder->coded_pkts_so_far[seqno];
	}
	// FEC phase is over
	else {
		already_sent_coded_packets = container->sent - seqno;
	}
	return already_sent_coded_packets;
}

/**
 * puts feedback packets into the encoder
 * @param encoder pointer to the encoder to work on
 * @param packet buffer with the packets
 *
 * @return 0 on success
 */
EXPORT
int nck_pacemg_enc_put_feedback(struct nck_pacemg_enc *encoder, struct sk_buff *packet) {
	if (encoder->feedback) {
		enc_container *cont_tmp, *cont_safe;

		//get decoder's oldest generation no
		assert(packet->len >= nck_pacemg_pkt_feedback_additional);
		uint32_t oldest_generation = skb_pull_u32(packet);
		uint32_t oldest_dec_global_seqno = skb_pull_u32(packet);
#ifdef ENC_PACKETS_FEEDBACK
		fprintf(stderr, ANSI_COLOR_RED "ENC  FB " ANSI_COLOR_YELLOW "====> %3d %4d: " ANSI_COLOR_BLUE, oldest_generation, oldest_dec_global_seqno);
#endif

		//delete older containers
		list_for_each_entry_safe(cont_tmp, cont_safe, &encoder->container_list, list) {
			if (cont_tmp->generation < oldest_generation) {
#ifdef ENC_PACKETS_FEEDBACK
				fprintf(stderr, "%2d del        ", cont_tmp->generation);
#endif
				nck_pacemg_enc_container_del(cont_tmp);
			}
		}

		//for every feedback
		while (packet->len >= nck_pacemg_pkt_feedback_size_single) {
			struct nck_pacemg_pkt_feedback feedback = {
					.generation = skb_pull_u32(packet),
					.rank_enc   = skb_pull_u16(packet),
					.rank_dec   = skb_pull_u16(packet),
					.seqno      = skb_pull_u16(packet),
			};
#ifdef ENC_PACKETS_FEEDBACK
			fprintf(stderr, ANSI_COLOR_RED "%2d %2d %2d ", feedback.generation, feedback.seqno, feedback.rank_dec);
#endif

			//calculate diffence between encoder's and decpder's rank
			int16_t rank_missing = feedback.rank_enc - feedback.rank_dec;
			assert(rank_missing >= 0);

#ifdef ENC_PACKETS_FEEDBACK
			int found = 0;
#endif

			//check for each container if it's generation is matching the feedback's
			list_for_each_entry_safe(cont_tmp, cont_safe, &encoder->container_list, list) {
				if (cont_tmp->generation == feedback.generation) {
#ifdef ENC_PACKETS_FEEDBACK
					fprintf(stderr, ANSI_COLOR_MAGENTA "ack" ANSI_COLOR_CYAN);
#endif
					if (rank_missing == 0) {
						if (cont_tmp->to_send == 0 && feedback.rank_dec == encoder->symbols) {
							//generation is fully delivered
							nck_pacemg_enc_container_del(cont_tmp);
#ifdef ENC_PACKETS_FEEDBACK
							fprintf(stderr, "d ");
						}
						else {
							fprintf(stderr, "  ");
#endif
						}
					}
					else {
#ifdef ENC_PACKETS_FEEDBACK
						fprintf(stderr, "  ");
#endif
						//subtract number of coded packets send since the packet the feedback is reacting to
						int coded_pkts_sent_after_seqno = 0;
						for (uint32_t idx=0; idx<encoder->max_cont_coded_history ; idx++) {
							if (cont_tmp->coded_pkts_seq_nos[idx] > oldest_dec_global_seqno) {
								coded_pkts_sent_after_seqno ++;
							}
						}
						rank_missing -= coded_pkts_sent_after_seqno;
//						rank_missing -= nck_pacemg_enc_additional_coded(cont_tmp, feedback.seqno);

						if (rank_missing > 0) {
							//increase number of packets to send
							cont_tmp->to_send += rank_missing;
                            nck_trigger_call(&encoder->on_coded_ready);
						}
					}
#ifdef ENC_PACKETS_FEEDBACK
					found = 1;
#endif
					break;
				}
			}
#ifdef ENC_PACKETS_FEEDBACK
			if (!found) {
				fprintf(stderr, ANSI_COLOR_YELLOW "nack ");
			}
#endif
		}
#ifdef ENC_PACKETS_FEEDBACK
		fprintf(stderr, ANSI_COLOR_RESET "\n");
#endif
	}
	return 0;
}
