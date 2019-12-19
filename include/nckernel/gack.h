#ifndef _NCK_GACK_H_
#define _NCK_GACK_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_gack_enc;
struct nck_gack_dec;
struct nck_gack_rec;
struct nck_timer;

/**
 * nck_gack_create_enc - creates a encoder for the gack protocol
 *
 * @encoder: encoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_gack_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_gack_create_dec - create a decoder for the gack protocol
 *
 * @decoder: decoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_gack_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_gack_create_rec - create a decoder for the gack protocol
 *
 * @recoder: recoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_gack_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_gack_enc *nck_gack_enc(krlnc_encoder_factory_t factory);
struct nck_gack_dec *nck_gack_dec(krlnc_decoder_factory_t factory);
struct nck_gack_rec *nck_gack_rec(krlnc_decoder_factory_t factory);

NCK_ENCODER_API(nck_gack)
NCK_DECODER_API(nck_gack)
NCK_RECODER_API(nck_gack)

char *nck_gack_dec_debug(void *dec);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_GACK_H_ */

