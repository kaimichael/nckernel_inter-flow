#ifndef _KODO_H_
#define _KODO_H_

#include <kodo_rlnc_c/common.h>
#include <kodo_rlnc_c/encoder.h>
#include <kodo_rlnc_c/decoder.h>
#include <nckernel/nckernel.h>

krlnc_encoder_t kodo_build_encoder(krlnc_encoder_factory_t factory);

krlnc_decoder_t kodo_build_decoder(krlnc_decoder_factory_t factory);

/**
 * Increment the index untin the next decoded symbol is found.
 *
 * @param decoder Kodo decoder
 * @param index Pointer to the current index
 */
void kodo_skip_undecoded(krlnc_decoder_t decoder, uint32_t *index);
/**
 * Add a source packet to the encoder.
 *
 * @param encoder Kodo encoder
 * @param packet Source packet
 * @param symbol_storage Memory block to store the symbols
 * @param index Index of the source packet
 * @returns 0 on success
 */
int kodo_put_source(krlnc_encoder_t encoder, struct sk_buff *packet, uint8_t *symbol_storage, uint32_t index);
/**
 * Retrieve a decoded source packet from the decoder.
 *
 * @param decoder Kodo decoder
 * @param packet Packet that will be filled with the decoded source data
 * @param symbol_storage Memory block where the symbols are stored
 * @param index Pointer to the current packet index. The value will be incremented to the next symbol.
 * @param flush When this flag is set uncoded symbols will be skipped.
 * @returns 0 on success
 */
int kodo_get_source(krlnc_decoder_t decoder, struct sk_buff *packet, uint8_t *symbol_storage, uint32_t *index, int flush);
/**
 * Retrieve all decoded packets and store them in a buffer.
 *
 * @param decoder Kodo decoder
 * @param symbol_storage Memory block where the symbols are stored
 * @param index Start index from which the source packets are enumerated
 * @param buffer Memory block where the decoded packets are stored
 * @returns Number of stored packets
 */
int kodo_flush_source(krlnc_decoder_t decoder, uint8_t *symbol_storage, uint32_t index, uint8_t *buffer);
/**
 * Give a coded packet to the decoder
 *
 * @param decoder Kodo decoder
 * @param packet Coded packet
 * @returns 0 on success
 */
int kodo_put_coded(krlnc_decoder_t decoder, struct sk_buff *packet);
/**
 * Retrieve a coded packet from an encoder
 * @param encoder Kodo encoder
 * @param packet Packet where the coded payload will be stored
 * @returns 0 on success
 */
int kodo_encoder_get_coded(krlnc_encoder_t encoder, struct sk_buff *packet);
int kodo_decoder_get_coded(krlnc_decoder_t encoder, struct sk_buff *packet);

int get_kodo_codec(krlnc_coding_vector_format *codec, const char *name);
int get_kodo_field(krlnc_finite_field *field, const char *name);
int get_kodo_enc_factory(krlnc_encoder_factory_t *factory, void *context, nck_opt_getter get_opt);
int get_kodo_dec_factory(krlnc_decoder_factory_t *factory, void *context, nck_opt_getter get_opt);

#endif /* _KODO_H_ */

