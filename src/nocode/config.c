#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/nocode.h>

#include "../private.h"
#include "../config.h"

EXPORT
int nck_nocode_enc_set_option(struct nck_nocode_enc *encoder, const char *name, const char *value)
{
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_nocode_dec_set_option(struct nck_nocode_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_nocode_rec_set_option(struct nck_nocode_rec *recoder, const char *name, const char *value)
{
	UNUSED(recoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_nocode_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbol_size = 1500;

	UNUSED(timer);

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	nck_nocode_enc_api(encoder, nck_nocode_enc(symbol_size));
	return 0;
}

EXPORT
int nck_nocode_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbol_size = 1500;

	UNUSED(timer);

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	nck_nocode_dec_api(decoder, nck_nocode_dec(symbol_size));
	return 0;
}

EXPORT
int nck_nocode_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbol_size = 1500;

	UNUSED(timer);

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	nck_nocode_rec_api(recoder, nck_nocode_rec(symbol_size));
	return 0;
}
