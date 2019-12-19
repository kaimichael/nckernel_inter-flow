#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/pace.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_pace_enc_set_option(struct nck_pace_enc *encoder, const char *name, const char *value)
{
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pace_dec_set_option(struct nck_pace_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pace_rec_set_option(struct nck_pace_rec *recoder, const char *name, const char *value)
{
	UNUSED(recoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_pace_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	krlnc_encoder_factory_t factory;
	struct nck_pace_enc *enc;
	const char *value;
	uint16_t pace_redundancy = 120;
	uint16_t tail_redundancy = 100;
	struct timeval redundancy_timeout = { 0, 20000 };

	if (get_kodo_enc_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo encoder factory.\n");
		return -1;
	}

	enc = nck_pace_enc(factory, timer);
	nck_pace_enc_api(encoder, enc);

	value = get_opt(context, "redundancy");
	if (nck_parse_u16(&pace_redundancy, value)) {
		fprintf(stderr, "Invalid pace_redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "pace_redundancy");
	if (nck_parse_u16(&pace_redundancy, value)) {
		fprintf(stderr, "Invalid pace_redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "tail_redundancy");
	if (nck_parse_u16(&tail_redundancy, value)) {
		fprintf(stderr, "Invalid tail_redundancy: %s\n", value);
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

	nck_pace_set_pace_redundancy(enc, pace_redundancy);
	nck_pace_set_tail_redundancy(enc, tail_redundancy);
	nck_pace_set_enc_redundancy_timeout(enc, &redundancy_timeout);
	return 0;
}

EXPORT
int nck_pace_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;
	struct nck_pace_dec *dec;
	const char *value;
	struct timeval fb_timeout = { 0, 10000 };

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	dec = nck_pace_dec(factory, timer);
	nck_pace_dec_api(decoder, dec);

	value = get_opt(context, "fb_timeout");
	if (nck_parse_timeval(&fb_timeout, value)) {
		fprintf(stderr, "Invalid fb_timeout: %s\n", value);
		return -1;
	}

	nck_pace_set_dec_fb_timeout(dec, &fb_timeout);

	return 0;
}

EXPORT
int nck_pace_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;
	struct nck_pace_rec *rec;
	const char *value;
	uint16_t pace_redundancy = 120;
	uint16_t tail_redundancy = 100;
	struct timeval rec_redundancy_timeout = { 0, 20000 };
	struct timeval rec_fb_timeout = { 0, 10000 };

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo recoder factory.\n");
		return -1;
	}

	rec = nck_pace_rec(factory, timer);
	nck_pace_rec_api(recoder, rec);

	value = get_opt(context, "pace_redundancy");
	if (nck_parse_u16(&pace_redundancy, value)) {
		fprintf(stderr, "Invalid pace_redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "tail_redundancy");
	if (nck_parse_u16(&tail_redundancy, value)) {
		fprintf(stderr, "Invalid tail_redundancy: %s\n", value);
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

	nck_pace_set_rec_pace_redundancy(rec, pace_redundancy);
	nck_pace_set_rec_tail_redundancy(rec, tail_redundancy);
	nck_pace_set_rec_fb_timeout(rec, &rec_fb_timeout);
	nck_pace_set_rec_redundancy_timeout(rec, &rec_redundancy_timeout);

	return 0;
}
