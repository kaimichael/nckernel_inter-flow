#ifndef _NCK_NOCODE_H_
#define _NCK_NOCODE_H_

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_nocode_enc;
struct nck_nocode_dec;
struct nck_nocode_rec;
struct nck_timer;

/**
 * nck_nocode_create_enc - creates a encoder for the nocode protocol
 *
 * @encoder: encoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_nocode_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_nocode_create_dec - create a decoder for the nocode protocol
 *
 * @decoder: decoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_nocode_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_nocode_create_rec - create a decoder for the nocode protocol
 *
 * @recoder: recoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_nocode_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_nocode_enc *nck_nocode_enc(size_t symbol_size);
struct nck_nocode_dec *nck_nocode_dec(size_t symbol_size);
struct nck_nocode_rec *nck_nocode_rec(size_t symbol_size);

NCK_ENCODER_API(nck_nocode)
NCK_DECODER_API(nck_nocode)
NCK_RECODER_API(nck_nocode)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_NOCODE_H_ */
