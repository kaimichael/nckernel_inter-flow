#ifndef _NCK_pacemg_H_
#define _NCK_pacemg_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_pacemg_enc;
struct nck_pacemg_dec;
struct nck_pacemg_rec;

struct nck_pacemg_dec_container;
struct nck_pacemg_rec_container;

int nck_pacemg_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_pacemg_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_pacemg_create_rec(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_pacemg_enc *nck_pacemg_enc(krlnc_encoder_factory_t factory, struct nck_timer *timer);
struct nck_pacemg_dec *nck_pacemg_dec(krlnc_decoder_factory_t factory, struct nck_timer *timer);
struct nck_pacemg_rec *nck_pacemg_rec(krlnc_decoder_factory_t factory, struct nck_timer *timer);

NCK_ENCODER_API(nck_pacemg)
NCK_DECODER_API(nck_pacemg)
NCK_RECODER_API(nck_pacemg)

//todo: implement the folllowing functions like in api.h
void nck_pacemg_set_enc_max_active_containers(struct nck_pacemg_enc *encoder, uint32_t max_active_containers);
void nck_pacemg_set_enc_coding_ratio(struct nck_pacemg_enc *encoder, uint16_t coding_ratio);
void nck_pacemg_set_enc_tail_packets(struct nck_pacemg_enc *encoder, uint16_t tail_packets);

void nck_pacemg_set_rec_coding_ratio(struct nck_pacemg_rec *recoder, uint16_t coding_ratio);
void nck_pacemg_set_rec_tail_packets(struct nck_pacemg_rec *recoder, uint16_t tail_packets);

void nck_pacemg_reserve_enc_coded_queue(struct nck_pacemg_enc *encoder);
void nck_pacemg_reserve_rec_cont_coded_queue(struct nck_pacemg_rec_container *container);

void nck_pacemg_set_enc_redundancy_timeout(struct nck_pacemg_enc *encoder, const struct timeval *enc_fb_timeout);
void nck_pacemg_set_enc_flush_timeout(struct nck_pacemg_enc *encoder, const struct timeval *enc_flush_timeout);

void nck_pacemg_set_dec_fb_timeout(struct nck_pacemg_dec *decoder, const struct timeval *dec_fb_timeout);
void nck_pacemg_set_dec_flush_timeout(struct nck_pacemg_dec *decoder, const struct timeval *dec_flush_timeout);

void nck_pacemg_set_rec_coding_ratio(struct nck_pacemg_rec *recoder, uint16_t coding_ratio);
void nck_pacemg_set_rec_tail_packets(struct nck_pacemg_rec *recoder, uint16_t tail_packets);

void nck_pacemg_set_feedback(struct nck_pacemg_enc *encoder, uint8_t feedback);

void nck_pacemg_set_enc_max_cont_coded_history(struct nck_pacemg_enc *encoder, uint32_t history_size);

void nck_pacemg_set_rec_fb_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_fb_timeout);
void nck_pacemg_set_rec_flush_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_flush_timeout);

void nck_pacemg_set_rec_fb_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_fb_timeout);
void nck_pacemg_set_rec_flush_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_flush_timeout);
void nck_pacemg_set_rec_redundancy_timeout(struct nck_pacemg_rec *recoder, const struct timeval *rec_redundancy_timeout);

void nck_pacemg_set_rec_max_active_containers(struct nck_pacemg_rec *recoder, uint16_t max_active_containers);
void nck_pacemg_set_dec_max_active_containers(struct nck_pacemg_dec *decoder, uint16_t max_active_containers);
void nck_pacemg_set_rec_max_containers(struct nck_pacemg_rec *recoder, uint16_t max_containers);


char *nck_pacemg_dec_debug(void *dec);
char *nck_pacemg_rec_debug(void *rec);
char *nck_pacemg_dec_contdebug(void *dec);
char *nck_pacemg_rec_contdebug(void *rec);

int nck_pacemg_set_enc_red_positions(struct nck_pacemg_enc *encoder, uint16_t coding_ratio, uint16_t tail_packets);
int nck_pacemg_set_rec_red_positions(struct nck_pacemg_rec *recoder, uint16_t coding_ratio, uint16_t tail_packets);

int nck_pacemg_rec_cont_has_coded(struct nck_pacemg_rec_container *container);


int nck_pacemg_dec_flush_container(struct nck_pacemg_dec_container *container);
//void nck_pacemg_rec_flush_container(struct nck_pacemg_rec_container *container);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_pacemg_H_ */

