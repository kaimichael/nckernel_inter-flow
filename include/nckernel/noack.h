#ifndef _NCK_NOACK_H_
#define _NCK_NOACK_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_noack_enc;
struct nck_noack_dec;
struct nck_noack_rec;
struct nck_timer;

/**
 * nck_noack_create_enc - creates a encoder for the noack protocol
 *
 * @encoder: encoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_noack_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_noack_create_dec - create a decoder for the noack protocol
 *
 * @decoder: decoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_noack_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_noack_create_rec - create a decoder for the noack protocol
 *
 * @recoder: recoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_noack_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_noack_enc *nck_noack_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer, int redundancy, int systematic, const struct timeval *timeout);
struct nck_noack_dec *nck_noack_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer, const struct timeval *timeout);
struct nck_noack_rec *nck_noack_rec(krlnc_decoder_factory_t factory, struct nck_timer *timer, int redundancy, const struct timeval *timeout);

void nck_noack_set_redundancy(struct nck_noack_enc *encoder, int redundancy);

NCK_ENCODER_API(nck_noack)
NCK_DECODER_API(nck_noack)
NCK_RECODER_API(nck_noack)

char *nck_noack_dec_debug(void *dec);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_NOACK_H_ */
