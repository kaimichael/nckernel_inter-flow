#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/nckernel.h>
#include <nckernel/tetrys.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_tetrys_enc_set_option(struct nck_tetrys_enc *encoder, const char *name, const char *value)
{
	if (!strcmp("systematic", name)) {
		uint32_t systematic = 0;
		if (nck_parse_u32(&systematic, value)) {
			return EINVAL;
		}

		nck_tetrys_enc_set_systematic_phase(encoder, systematic);
	} else if (!strcmp("coded", name)) {
		uint32_t coded = 1;
		if (nck_parse_u32(&coded, value)) {
			return EINVAL;
		}

		nck_tetrys_enc_set_coded_phase(encoder, coded);
	} else {
		return ENOTSUP;
	}

	return 0;
}

EXPORT
int nck_tetrys_dec_set_option(struct nck_tetrys_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_tetrys_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbol_size = 1500, window_size = 16;
	struct timeval timeout = { 0, 100000 };
	struct nck_tetrys_enc *enc;

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	value = get_opt(context, "window_size");
	if (nck_parse_u32(&window_size, value)) {
		fprintf(stderr, "Invalid window: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (!timer) {
		assert(value == NULL);
		timerclear(&timeout);
	} else {
		if (nck_parse_timeval(&timeout, value)) {
			fprintf(stderr, "Invalid timeout: %s\n", value);
			return -1;
		}
	}

	enc = nck_tetrys_enc(symbol_size, window_size, timer, &timeout);

	value = get_opt(context, "systematic");
	if (value) {
		nck_tetrys_enc_set_option(enc, "systematic", value);
	}

	value = get_opt(context, "coded");
	if (value) {
		nck_tetrys_enc_set_option(enc, "coded", value);
	}

	nck_tetrys_enc_api(encoder, enc);

	return 0;
}

EXPORT
int nck_tetrys_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbol_size = 1500, window_size = 16;

	UNUSED(timer);

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	value = get_opt(context, "window_size");
	if (nck_parse_u32(&window_size, value)) {
		fprintf(stderr, "Invalid window_size: %s\n", value);
		return -1;
	}

	nck_tetrys_dec_api(decoder, nck_tetrys_dec(symbol_size, window_size));
	return 0;
}
