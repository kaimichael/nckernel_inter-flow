#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/pacemg.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_pacemg_enc_set_option(struct nck_pacemg_enc *encoder, const char *name, const char *value) {
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pacemg_dec_set_option(struct nck_pacemg_dec *decoder, const char *name, const char *value) {
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pacemg_rec_set_option(struct nck_pacemg_rec *recoder, const char *name, const char *value) {
	UNUSED(recoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pacemg_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt) {
	krlnc_encoder_factory_t factory;
	struct nck_pacemg_enc *enc;
	const char *value;
	uint32_t max_active_containers = 8;
	uint32_t max_history_size = 64;
	uint16_t coding_ratio = 100;
	uint16_t tail_packets = 0;
	struct timeval redundancy_timeout = {0, 20000};
	uint8_t feedback = 0;

	if (get_kodo_enc_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo encoder factory.\n");
		return -1;
	}

	enc = nck_pacemg_enc(factory, timer);
	nck_pacemg_enc_api(encoder, enc);

	value = get_opt(context, "max_active_containers");
	if (nck_parse_u32(&max_active_containers, value)) {
		fprintf(stderr, "Invalid max_active_containers: %s\n", value);
		return -1;
	}

	value = get_opt(context, "max_history");
	if (nck_parse_u32(&max_history_size, value)) {
		fprintf(stderr, "Invalid max_history: %s\n", value);
		return -1;
	}

	value = get_opt(context, "coding_ratio");
	if (nck_parse_u16(&coding_ratio, value)) {
		fprintf(stderr, "Invalid coding_ratio: %s\n", value);
		return -1;
	}

	value = get_opt(context, "redundancy");
	if (nck_parse_u16(&coding_ratio, value)) {
		fprintf(stderr, "Invalid redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "tail_packets");
	if (nck_parse_u16(&tail_packets, value)) {
		fprintf(stderr, "Invalid tail_packets: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&redundancy_timeout, value)) {
		fprintf(stderr, "Invalid redundancy_timeout: %s\n", value);
		return -1;
	}

	value = get_opt(context, "redundancy_timeout");
	if (nck_parse_timeval(&redundancy_timeout, value)) {
		fprintf(stderr, "Invalid redundancy_timeout: %s\n", value);
		return -1;
	}

	value = get_opt(context, "feedback");
	if (nck_parse_u8(&feedback, value)) {
		fprintf(stderr, "Invalid feedback: %s\n", value);
		return -1;
	}

	nck_pacemg_set_enc_max_active_containers(enc, max_active_containers);
	nck_pacemg_set_enc_max_cont_coded_history(enc, max_history_size);
	nck_pacemg_set_enc_coding_ratio(enc, coding_ratio);
	nck_pacemg_set_enc_tail_packets(enc, tail_packets);
	nck_pacemg_set_enc_redundancy_timeout(enc, &redundancy_timeout);
	nck_pacemg_set_feedback(enc, feedback);
	nck_pacemg_reserve_enc_coded_queue(enc);

	return 0;
}

EXPORT
int nck_pacemg_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt) {
	krlnc_decoder_factory_t factory;
	struct nck_pacemg_dec *dec;
	const char *value;
	struct timeval fb_timeout = {0, 10000};
	uint16_t max_active_containers = 8;

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	dec = nck_pacemg_dec(factory, timer);
	nck_pacemg_dec_api(decoder, dec);

	value = get_opt(context, "fb_timeout");
	if (nck_parse_timeval(&fb_timeout, value)) {
		fprintf(stderr, "Invalid fb_timeout: %s\n", value);
		return -1;
	}
	value = get_opt(context, "max_active_containers");
	if (nck_parse_u16(&max_active_containers, value)) {
		fprintf(stderr, "Invalid fb_timeout: %s\n", value);
		return -1;
	}

	nck_pacemg_set_dec_max_active_containers(dec, max_active_containers);
	nck_pacemg_set_dec_fb_timeout(dec, &fb_timeout);

	return 0;
}

EXPORT
int nck_pacemg_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt) {
	krlnc_decoder_factory_t factory;
	struct nck_pacemg_rec *rec;
	const char *value;
	uint16_t coding_ratio = 100;
	uint16_t tail_packets = 0;
	uint16_t max_active_containers = 8;
	uint16_t max_containers = 16;
	struct timeval rec_redundancy_timeout = {0, 20000};
	struct timeval rec_fb_timeout = {0, 10000};
	struct timeval rec_flush_timeout = {0, 500000};

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo recoder factory.\n");
		return -1;
	}

	rec = nck_pacemg_rec(factory, timer);
	nck_pacemg_rec_api(recoder, rec);

	value = get_opt(context, "redundancy");
	if (nck_parse_u16(&coding_ratio, value)) {
		fprintf(stderr, "Invalid coding_ratio: %s\n", value);
		return -1;
	}

	value = get_opt(context, "coding_ratio");
	if (nck_parse_u16(&coding_ratio, value)) {
		fprintf(stderr, "Invalid coding_ratio: %s\n", value);
		return -1;
	}

	value = get_opt(context, "max_active_containers");
	if (nck_parse_u16(&max_active_containers, value)) {
		fprintf(stderr, "Invalid coding_ratio: %s\n", value);
		return -1;
	}

	value = get_opt(context, "max_containers");
	if (nck_parse_u16(&max_containers, value)) {
		fprintf(stderr, "Invalid coding_ratio: %s\n", value);
		return -1;
	}

	value = get_opt(context, "tail_packets");
	if (nck_parse_u16(&tail_packets, value)) {
		fprintf(stderr, "Invalid tail_packets: %s\n", value);
		return -1;
	}

	value = get_opt(context, "redundancy_timeout");
	if (nck_parse_timeval(&rec_redundancy_timeout, value)) {
		fprintf(stderr, "Invalid rec_redundancy_timeout: %s\n", value);
		return -1;
	}

	value = get_opt(context, "fb_timeout");
	if (nck_parse_timeval(&rec_fb_timeout, value)) {
		fprintf(stderr, "Invalid rec_fb_timeout: %s\n", value);
		return -1;
	}

	nck_pacemg_set_rec_coding_ratio(rec, coding_ratio);
	nck_pacemg_set_rec_tail_packets(rec, tail_packets);
	nck_pacemg_set_rec_fb_timeout(rec, &rec_fb_timeout);
	nck_pacemg_set_rec_redundancy_timeout(rec, &rec_redundancy_timeout);
	nck_pacemg_set_rec_flush_timeout(rec,&rec_flush_timeout);
	nck_pacemg_set_rec_max_active_containers(rec, max_active_containers);
	nck_pacemg_set_rec_max_containers(rec, max_containers);


	return 0;
}
