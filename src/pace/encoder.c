#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/pace.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#include "../private.h"
#include "../kodo.h"
#include "../util/helper.h"


struct nck_pace_enc {
	krlnc_encoder_factory_t factory;
	krlnc_encoder_t coder;

	uint32_t generation;
	uint32_t block_size;
	uint32_t symbols;
	uint32_t rank;

	uint32_t pace_redundancy;
	uint32_t tail_redundancy;

	struct nck_timer *timer;
	size_t source_size, coded_size, feedback_size;

	struct timeval enc_redundancy_timeout;
	struct nck_timer_entry *enc_redundancy_timeout_handle;

	struct timeval enc_flush_timeout;
	struct nck_timer_entry *enc_flush_timeout_handle;

	struct nck_trigger on_coded_ready;

	uint32_t last_fb_rank;

	int complete;
	uint32_t to_send;

	uint8_t *buffer;
};

NCK_ENCODER_IMPL(nck_pace, NULL, NULL, NULL)

static void enc_start_next_generation(struct nck_pace_enc *encoder, uint32_t generation) {
	//fprintf(stderr, "\nEnc - Start next gen: %d", generation);
	encoder->generation = generation;
	encoder->rank = 0;
	encoder->complete = 0;
	encoder->to_send = 0;
	encoder->last_fb_rank = 0;

	if (encoder->coder) {
		krlnc_delete_encoder(encoder->coder);
	}
	encoder->coder = kodo_build_encoder(encoder->factory);
	memset(encoder->buffer, 0, krlnc_encoder_block_size(encoder->coder));
}

EXPORT
struct nck_pace_enc *nck_pace_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer) {
	struct nck_pace_enc *result;
	uint32_t block_size;
	krlnc_encoder_t enc;

	// create an encoder to get some parameters out
	enc = kodo_build_encoder(factory);
	block_size = krlnc_encoder_block_size(enc);

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_coded_ready);

	result->buffer = malloc(block_size);

	result->symbols = krlnc_encoder_factory_symbols(factory);

	result->factory = factory;

	enc_start_next_generation(result, 1);

	result->source_size = krlnc_encoder_factory_symbol_size(factory);
	result->coded_size = krlnc_encoder_payload_size(enc) + 6;
	result->feedback_size = 6;

	UNUSED(timer);
//	result->timer = timer;		// pointer?
//	result->enc_redundancy_timeout_handle = nck_timer_add(timer, NULL, result, encoder_send_redundancy);
//	result->enc_flush_timeout_handle = nck_timer_add(timer, NULL, result, encoder_timeout_flush);

	nck_pace_set_pace_redundancy(result, 120);
	nck_pace_set_tail_redundancy(result, 100);

	struct timeval timeout_redundancy, timeout_flush;
	nck_pace_set_enc_redundancy_timeout(result, double_to_tv(0.100, &timeout_redundancy));
	nck_pace_set_enc_flush_timeout(result, double_to_tv(60.0, &timeout_flush));

	krlnc_delete_encoder(enc);

	return result;
}

EXPORT
void nck_pace_set_pace_redundancy(struct nck_pace_enc *encoder, uint16_t pace_redundancy) {
	encoder->pace_redundancy = pace_redundancy;
}

EXPORT
void nck_pace_set_tail_redundancy(struct nck_pace_enc *encoder, uint16_t tail_redundancy) {
	encoder->tail_redundancy = tail_redundancy;
}

EXPORT
void nck_pace_set_enc_redundancy_timeout(struct nck_pace_enc *encoder, const struct timeval *enc_fb_timeout) {
	if (enc_fb_timeout != NULL) {
		encoder->enc_redundancy_timeout = *enc_fb_timeout;
	} else {
		timerclear(&encoder->enc_redundancy_timeout);
	}
}

EXPORT
void nck_pace_set_enc_flush_timeout(struct nck_pace_enc *encoder, const struct timeval *enc_flush_timeout) {
	if (enc_flush_timeout != NULL) {
		encoder->enc_flush_timeout = *enc_flush_timeout;
	} else {
		timerclear(&encoder->enc_flush_timeout);
	}
}


EXPORT
void nck_pace_enc_free(struct nck_pace_enc *encoder) {
	krlnc_delete_encoder(encoder->coder);
	krlnc_delete_encoder_factory(encoder->factory);

	nck_timer_cancel(encoder->enc_redundancy_timeout_handle);
	nck_timer_free(encoder->enc_redundancy_timeout_handle);

	nck_timer_cancel(encoder->enc_flush_timeout_handle);
	nck_timer_free(encoder->enc_flush_timeout_handle);

	free(encoder->buffer);
	free(encoder);
}

EXPORT
int nck_pace_enc_has_coded(struct nck_pace_enc *encoder) {
	return encoder->to_send >= 100;
}

EXPORT
int nck_pace_enc_full(struct nck_pace_enc *encoder) {
	int ret;
	ret = encoder->rank == encoder->symbols &&
	      encoder->to_send >= 100;
	return ret;
}

EXPORT
int nck_pace_enc_complete(struct nck_pace_enc *encoder) {
	return encoder->complete;
}

EXPORT
void nck_pace_enc_flush_coded(struct nck_pace_enc *encoder) {
	encoder->to_send += encoder->tail_redundancy;
	nck_trigger_call(&encoder->on_coded_ready);
}

EXPORT
int nck_pace_enc_get_coded(struct nck_pace_enc *encoder, struct sk_buff *packet) {
	if (!_has_coded(encoder)) {
		return -1;
	}

	skb_reserve(packet, 6);
	kodo_encoder_get_coded(encoder->coder, packet);
	skb_push_u16(packet, (uint16_t) encoder->rank);
	skb_push_u32(packet, encoder->generation);

	encoder->to_send -= 100;

	return 0;
}

EXPORT
int nck_pace_enc_put_source(struct nck_pace_enc *encoder, struct sk_buff *packet) {
	if (encoder->rank == encoder->symbols) {
		enc_start_next_generation(encoder, encoder->generation + 1);
	}

	kodo_put_source(encoder->coder, packet, encoder->buffer, encoder->rank);

	encoder->rank++;
	encoder->to_send += encoder->pace_redundancy;
	// fprintf(stderr, "\nEnc rank: %d symbols: %d", encoder->rank, encoder->symbols);

	if (encoder->rank == encoder->symbols) {
		_flush_coded(encoder);
	}

	if (timerisset(&encoder->enc_flush_timeout)) {
		nck_timer_rearm(encoder->enc_flush_timeout_handle, &encoder->enc_flush_timeout);
	}

	if (timerisset(&encoder->enc_redundancy_timeout)) {
		nck_timer_rearm(encoder->enc_redundancy_timeout_handle, &encoder->enc_redundancy_timeout);
	}

	nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_pace_enc_put_feedback(struct nck_pace_enc *encoder, struct sk_buff *packet) {
	uint32_t rank, generation;

	generation = skb_pull_u32(packet);
	rank = skb_pull_u16(packet);

	if (generation > encoder->generation) {
		// Impossible state
		return -1;
	}
	//fprintf(stderr, "\nRx feedback >> Gen: %d Rank: %d \tEncGen: %d EncRank: %d",
	//		generation, rank, encoder->generation, encoder->rank);

	if (generation == encoder->generation) {
		encoder->last_fb_rank = rank;
		if (rank == encoder->rank) {
			encoder->to_send = 0;
		}
		if (rank == encoder->symbols) {
			encoder->complete = 1;
		}
	}

	return 0;
}



