#ifndef _NCK_TETRYS_H_
#define _NCK_TETRYS_H_

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_tetrys_enc;
struct nck_tetrys_dec;
struct nck_timer;

/**
 * nck_tetrys_create_enc - creates a encoder for the tetrys protocol
 *
 * @encoder: encoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_tetrys_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_tetrys_create_dec - create a decoder for the tetrys protocol
 *
 * @decoder: decoder structure that will be configured
 * @timer: timer implementation that will be used by the encoder
 * @context: configuration context (e.g. a file, a dict structure, ...)
 * @get_opt: a function to extract a configuration value from the context
 */
int nck_tetrys_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_tetrys_enc *nck_tetrys_enc(size_t symbol_size, int window_size, struct nck_timer *timer, const struct timeval *timeout);
struct nck_tetrys_dec *nck_tetrys_dec(size_t symbol_size, int window_size);

/**
 * Set the length of the systematic phase.
 *
 * This is the number of uncoded packets that will be sent before the next coded packet.
 * For example setting this to 3 ensures that every 3 packets a coded packet is sent.
 *
 * @encoder: encoder structure to configure
 * @phase_length: number of uncoded packets to send in a row
 */
void nck_tetrys_enc_set_systematic_phase(struct nck_tetrys_enc *encoder, uint32_t phase_length);
/**
 * Set the length of the coded phase.
 *
 * This is the number of coded packets that will be sent before the next uncoded packet.
 *
 * @encoder: encoedr structure to configure
 * @phase_length: number of coded packets to send in a row
 */
void nck_tetrys_enc_set_coded_phase(struct nck_tetrys_enc *encoder, uint32_t phase_length);

NCK_ENCODER_API(nck_tetrys)
NCK_DECODER_API(nck_tetrys)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_TETRYS_H_ */
