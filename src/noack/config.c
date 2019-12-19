#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/noack.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_noack_enc_set_option(struct nck_noack_enc *encoder, const char *name, const char *value)
{
	if (!strcmp("redundancy", name)) {
		uint32_t redundancy = 3;
		if (nck_parse_u32(&redundancy, value)) {
			return EINVAL;
		}

		nck_noack_set_redundancy(encoder, redundancy);
	}
	return 0;
}

EXPORT
int nck_noack_dec_set_option(struct nck_noack_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_noack_rec_set_option(struct nck_noack_rec *recoder, const char *name, const char *value)
{
	UNUSED(recoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_noack_create_enc(struct nck_encoder *encoder, struct nck_timer *timer,
			 void *context, nck_opt_getter get_opt)
{
	const char *value;
	krlnc_encoder_factory_t factory;
	uint32_t redundancy = 3;
	uint32_t systematic = 1;
	struct timeval timeout = { 0, 100000 };
	struct nck_noack_enc *enc;

	if (get_kodo_enc_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo encoder factory.\n");
		return -1;
	}

	value = get_opt(context, "redundancy");
	if (nck_parse_u32(&redundancy, value)) {
		fprintf(stderr, "Invalid redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&timeout, value)) {
		fprintf(stderr, "Invalid timeout: %s\n", value);
		return -1;
	}

	value = get_opt(context, "systematic");
	if (nck_parse_u32(&systematic, value)) {
		fprintf(stderr, "Invalid 'Systematic' mode: %s\n", value);
		return -1;
	}

	enc = nck_noack_enc(factory, timer, redundancy, systematic, &timeout);

	nck_noack_enc_api(encoder, enc);
	return 0;
}

EXPORT
int nck_noack_create_dec(struct nck_decoder *decoder, struct nck_timer *timer,
			 void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;
	struct timeval timeout = { 0, 500000 };
	const char *value;

	UNUSED(timer);

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&timeout, value)) {
		fprintf(stderr, "Invalid timeout: %s\n", value);
		return -1;
	}

	nck_noack_dec_api(decoder, nck_noack_dec(factory, timer, &timeout));
	return 0;
}

EXPORT
int nck_noack_create_rec(struct nck_recoder *recoder, struct nck_timer *timer,
			 void *context, nck_opt_getter get_opt)
{
	const char *value;
	krlnc_decoder_factory_t factory;
	uint32_t redundancy = 3;
	struct timeval timeout = { 0, 100000 };
	struct nck_noack_rec *rec;

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	value = get_opt(context, "redundancy");
	if (nck_parse_u32(&redundancy, value)) {
		fprintf(stderr, "Invalid redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&timeout, value)) {
		fprintf(stderr, "Invalid timeout: %s\n", value);
		return -1;
	}

	rec = nck_noack_rec(factory, timer, redundancy, &timeout);

	nck_noack_rec_api(recoder, rec);
	return 0;
}
