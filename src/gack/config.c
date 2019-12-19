#include <nckernel/nckernel.h>
#include <nckernel/gack.h>

#include "../private.h"
#include "../config.h"
#include "../kodo.h"

EXPORT
int nck_gack_enc_set_option(struct nck_gack_enc *encoder, const char *name, const char *value)
{
	UNUSED(encoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_gack_dec_set_option(struct nck_gack_dec *decoder, const char *name, const char *value)
{
	UNUSED(decoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_gack_rec_set_option(struct nck_gack_rec *recoder, const char *name, const char *value)
{
	UNUSED(recoder);
	UNUSED(name);
	UNUSED(value);
	return 0;
}

EXPORT
int nck_gack_create_enc(struct nck_encoder *encoder, struct nck_timer *timer,
			void *context, nck_opt_getter get_opt)
{
	krlnc_encoder_factory_t factory;

	UNUSED(timer);

	if (get_kodo_enc_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo encoder factory.\n");
		return -1;
	}

	nck_gack_enc_api(encoder, nck_gack_enc(factory));
	return 0;
}

EXPORT
int nck_gack_create_dec(struct nck_decoder *decoder, struct nck_timer *timer,
			void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;

	UNUSED(timer);

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	nck_gack_dec_api(decoder, nck_gack_dec(factory));
	return 0;
}

EXPORT
int nck_gack_create_rec(struct nck_recoder *recoder, struct nck_timer *timer,
			void *context, nck_opt_getter get_opt)
{
	krlnc_decoder_factory_t factory;

	UNUSED(timer);

	if (get_kodo_dec_factory(&factory, context, get_opt)) {
		fprintf(stderr, "Failed to create the kodo decoder factory.\n");
		return -1;
	}

	nck_gack_rec_api(recoder, nck_gack_rec(factory));
	return 0;
}

