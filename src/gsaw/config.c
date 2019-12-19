#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/gsaw.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_gsaw_enc_set_option(struct nck_gsaw_enc *encoder, const char *name, const char *value)
{
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_gsaw_dec_set_option(struct nck_gsaw_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_gsaw_create_enc(struct nck_encoder *encoder, struct nck_timer *timer,
			void *context, nck_opt_getter get_opt)
{
    const char *value;
    krlnc_encoder_factory_t factory;
    uint32_t redundancy = 0;

    UNUSED(timer);

    if (get_kodo_enc_factory(&factory, context, get_opt)) {
        fprintf(stderr, "Failed to create the kodo encoder factory.\n");
        return -1;
    }

    value = get_opt(context, "redundancy");
    if (nck_parse_u32(&redundancy, value)) {
	fprintf(stderr, "Invalid redundancy: %u\n", redundancy);
	return -1;
    }

    nck_gsaw_enc_api(encoder, nck_gsaw_enc(factory, redundancy));
    return 0;
}

EXPORT
int nck_gsaw_create_dec(struct nck_decoder *decoder, struct nck_timer *timer,
			void *context, nck_opt_getter get_opt)
{
    krlnc_decoder_factory_t factory;

    UNUSED(timer);

    if (get_kodo_dec_factory(&factory, context, get_opt)) {
        fprintf(stderr, "Failed to create the kodo decoder factory.\n");
        return -1;
    }

    nck_gsaw_dec_api(decoder, nck_gsaw_dec(factory));
    return 0;
}

