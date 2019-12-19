#ifndef _NCK_PACE_H_
#define _NCK_PACE_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_pace_enc;
struct nck_pace_dec;
struct nck_pace_rec;

int nck_pace_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_pace_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_pace_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_pace_enc *nck_pace_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer);
struct nck_pace_dec *nck_pace_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer);
struct nck_pace_rec *nck_pace_rec(krlnc_decoder_factory_t factory, struct nck_timer *timer);

NCK_ENCODER_API(nck_pace)
NCK_DECODER_API(nck_pace)
NCK_RECODER_API(nck_pace)

//todo: implement the folllowing functions like in api.h
void nck_pace_set_pace_redundancy(struct nck_pace_enc *encoder, uint16_t pace_redundancy);
void nck_pace_set_tail_redundancy(struct nck_pace_enc *encoder, uint16_t tail_redundancy);

void nck_pace_set_enc_redundancy_timeout(struct nck_pace_enc *encoder, const struct timeval *enc_fb_timeout);
void nck_pace_set_enc_flush_timeout(struct nck_pace_enc *encoder, const struct timeval *enc_flush_timeout);

void nck_pace_set_dec_fb_timeout(struct nck_pace_dec *decoder, const struct timeval *dec_fb_timeout);
void nck_pace_set_dec_flush_timeout(struct nck_pace_dec *decoder, const struct timeval *dec_flush_timeout);

void nck_pace_set_rec_pace_redundancy(struct nck_pace_rec *recoder, uint16_t pace_redundancy);
void nck_pace_set_rec_tail_redundancy(struct nck_pace_rec *recoder, uint16_t tail_redundancy);

void nck_pace_set_rec_fb_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_fb_timeout);
void nck_pace_set_rec_flush_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_flush_timeout);

void nck_pace_set_rec_fb_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_fb_timeout);
void nck_pace_set_rec_flush_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_flush_timeout);
void nck_pace_set_rec_redundancy_timeout(struct nck_pace_rec *recoder, const struct timeval *rec_redundancy_timeout);

char *nck_pace_dec_debug(void *dec);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_PACE_H_ */

