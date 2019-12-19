#ifndef _NCK_GSAW_H_
#define _NCK_GSAW_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nck_gsaw_enc;
struct nck_gsaw_dec;

int nck_gsaw_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
int nck_gsaw_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

struct nck_gsaw_enc *nck_gsaw_enc(krlnc_encoder_factory_t factory, int redundancy);
struct nck_gsaw_dec *nck_gsaw_dec(krlnc_decoder_factory_t factory);

void nck_gsaw_set_redundancy(struct nck_gsaw_enc *encoder, int redundancy);

NCK_ENCODER_API(nck_gsaw)
NCK_DECODER_API(nck_gsaw)

char *nck_gsaw_dec_debug(void *dec);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_GSAW_H_ */

