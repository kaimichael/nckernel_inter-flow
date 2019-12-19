#ifndef _NCK_CODARQ_H_
#define _NCK_CODARQ_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_codarq_enc;
struct nck_codarq_dec;

struct nck_codarq_dec_container;
struct nck_codarq_enc_container;

int nck_codarq_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_codarq_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_codarq_enc *nck_codarq_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer);
struct nck_codarq_dec *nck_codarq_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer);

NCK_ENCODER_API(nck_codarq)
NCK_DECODER_API(nck_codarq)

//todo: implement the folllowing functions like in api.h
void nck_codarq_set_redundancy(struct nck_codarq_enc *encoder, uint16_t redundancy);

void nck_codarq_set_dec_fb_timeout(struct nck_codarq_dec *decoder, const struct timeval *dec_fb_timeout);
void nck_codarq_set_dec_flush_timeout(struct nck_codarq_dec *decoder, const struct timeval *dec_flush_timeout);

void nck_codarq_set_enc_max_active_containers(struct nck_codarq_enc *encoder, uint8_t max_active_containers);
void nck_codarq_set_enc_repair_timeout(struct nck_codarq_enc *encoder, const struct timeval *enc_repair_timeout);

char *nck_codarq_dec_debug(void *dec);

int nck_codarq_enc_set_red_positions(struct nck_codarq_enc *encoder, uint16_t pace_redundancy, uint16_t tail_redundancy);


void nck_codarq_dec_flush_container(struct nck_codarq_dec_container *container);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_CODARQ_H_ */

