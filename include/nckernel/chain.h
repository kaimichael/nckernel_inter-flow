#ifndef _NCK_CHAIN_H_
#define _NCK_CHAIN_H_

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_chain_enc;
struct nck_chain_dec;
struct nck_chain_rec;
struct nck_timer;

/**
 * nck_chain_create_enc - creates an encoder based on a chain of encoders
 *
 * @encoder: encoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_chain_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_chain_create_dec - create a decoder based on a chain of decoders
 *
 * @decoder: decoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_chain_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

/**
 * nck_chain_enc - create chain encoder
 *
 * @stages: pointer to an array of encoders
 * @stage_count: number of encoders in the array
 */
struct nck_chain_enc *nck_chain_enc(struct nck_encoder *stages, unsigned int stage_count);
/**
 * nck_chain_dec - create chain decoder
 *
 * @stages: pointer to an array of decoders
 * @stage_count: number of decoders in the array
 */
struct nck_chain_dec *nck_chain_dec(struct nck_decoder *stages, unsigned int stage_count);

int nck_chain_enc_set_stage_option(struct nck_chain_enc *encoder, unsigned int stage, const char *name, const char *value);
int nck_chain_dec_set_stage_option(struct nck_chain_dec *decoder, unsigned int stage, const char *name, const char *value);

NCK_ENCODER_API(nck_chain)
NCK_DECODER_API(nck_chain)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_CHAIN_H_ */
