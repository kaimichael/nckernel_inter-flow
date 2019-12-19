#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/codarq.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_codarq_enc_set_option(struct nck_codarq_enc *encoder, const char *name, const char *value)
{
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_codarq_dec_set_option(struct nck_codarq_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_codarq_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	krlnc_encoder_factory_t factory;
	struct nck_codarq_enc *enc;
	const char *value;
	uint16_t redundancy = 2;
	uint8_t max_active_containers = 8;
	struct timeval repair_timeout =   { 0, 100000 };

	if (get_kodo_enc_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo encoder factory.\n");
		return -1;
	}

	enc = nck_codarq_enc(factory, timer);
	nck_codarq_enc_api(encoder, enc);

	value = get_opt(context, "redundancy");
	if (nck_parse_u16(&redundancy, value)) {
		fprintf(stderr, "Invalid redundancy: %s\n", value);
		return -1;
	}

	value = get_opt(context, "max_containers");
	if (nck_parse_u8(&max_active_containers, value)) {
		fprintf(stderr, "Invalid max_containers: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&repair_timeout, value)) {
		fprintf(stderr, "Invalid repair_timeout: %s\n", value);
		return -1;
	}

	nck_codarq_set_redundancy(enc, redundancy);
	nck_codarq_set_enc_repair_timeout(enc,&repair_timeout);
	nck_codarq_set_enc_max_active_containers(enc, max_active_containers);

	return 0;
}

EXPORT
int nck_codarq_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;
	struct nck_codarq_dec *dec;
	const char *value;
	struct timeval fb_timeout = { 0, 50000 };

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	dec = nck_codarq_dec(factory, timer);
	nck_codarq_dec_api(decoder, dec);

	value = get_opt(context, "fb_timeout");
	if (nck_parse_timeval(&fb_timeout, value)) {
		fprintf(stderr, "Invalid fb_timeout: %s\n", value);
		return -1;
	}

	nck_codarq_set_dec_fb_timeout(dec, &fb_timeout);

	return 0;
}
