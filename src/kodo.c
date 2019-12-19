#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/time.h>
#include <kodo_rlnc_c/common.h>
#include <nckernel/skb.h>

#include "kodo.h"
#include "config.h"

krlnc_encoder_t kodo_build_encoder(krlnc_encoder_factory_t factory)
{
	krlnc_encoder_t enc;
	enc = krlnc_encoder_factory_build(factory);
	krlnc_encoder_set_seed(enc, rand());
	return enc;
}

krlnc_decoder_t kodo_build_decoder(krlnc_decoder_factory_t factory)
{
	krlnc_decoder_t dec;
	dec = krlnc_decoder_factory_build(factory);
	krlnc_decoder_set_seed(dec, rand());
	return dec;
}

/**
 * kodo_skip_undecoded - sets index to the next decoded symbol or the end of
 *   the generation
 *
 * @decoder: kodo decoder to work on
 * @index: index pointing to the current unprocessed symbol
 */
void kodo_skip_undecoded(krlnc_decoder_t decoder, uint32_t *index)
{
	uint32_t symbols = krlnc_decoder_symbols(decoder);

	/* increase index until a decoded symbol has been found */
	for ( ; *index < symbols ; (*index)++)
		if (krlnc_decoder_is_symbol_uncoded(decoder, *index))
			break;
}

int kodo_put_source(krlnc_encoder_t encoder, struct sk_buff *packet, uint8_t
		*symbol_storage, uint32_t index)
{
	uint32_t symbol_size;
	uint8_t *symbol;
	int rest;

	symbol_size = krlnc_encoder_symbol_size(encoder);

	rest = symbol_size - packet->len;
	if (rest < 0) {
		fprintf(stderr, "packet length exceeded by %d bytes\n", -rest);
		return -1;
	}

	symbol = &symbol_storage[index * symbol_size];
	memcpy(symbol, packet->data, packet->len);
	memset(symbol + packet->len, 0, rest);

	krlnc_encoder_set_const_symbol(encoder, index, symbol, symbol_size);

	return 0;
}

int kodo_get_source(krlnc_decoder_t decoder, struct sk_buff *packet, uint8_t
		*symbol_storage, uint32_t *index, int flush)
{
	uint8_t *ptr, *payload;
	size_t len;

	len = krlnc_decoder_symbol_size(decoder);
	ptr = &symbol_storage[*index * len];

	payload = skb_put(packet, len);
	memcpy(payload, ptr, len);

	*index += 1;
	if (flush) {
		kodo_skip_undecoded(decoder, index);
	}

	return 0;
}

int kodo_flush_source(krlnc_decoder_t decoder, uint8_t *symbol_storage, uint32_t index, uint8_t *buffer)
{
	uint32_t symbol_size, symbols;
	int count = 0;

	symbol_size = krlnc_decoder_symbol_size(decoder);
	symbols = krlnc_decoder_symbols(decoder);

	for ( ; index < symbols; ++index) {
		if (krlnc_decoder_is_symbol_uncoded(decoder, index)) {
			memcpy(buffer + count*symbol_size, symbol_storage + index*symbol_size, symbol_size);
			count += 1;
		}
	}

	return count;
}

int kodo_put_coded(krlnc_decoder_t decoder, struct sk_buff *packet)
{
	int symbol_size;

	symbol_size = krlnc_decoder_symbol_size(decoder);
	skb_put_zeros(packet, symbol_size);

	krlnc_decoder_read_payload(decoder, packet->data);
	return 0;
}

int kodo_decoder_get_coded(krlnc_decoder_t decoder, struct sk_buff *packet)
{
	uint8_t *payload;
	size_t payload_size, real_size;

	payload_size = krlnc_decoder_payload_size(decoder);
	payload = skb_put(packet, payload_size);
	real_size = krlnc_decoder_write_payload(decoder, payload);
	if (payload_size > packet->len) {
		skb_trim(packet, payload_size - real_size);
	}
	skb_trim_zeros(packet);

	return 0;
}

int kodo_encoder_get_coded(krlnc_encoder_t encoder, struct sk_buff *packet)
{
	uint8_t *payload;
	size_t payload_size, real_size;

	payload_size = krlnc_encoder_payload_size(encoder);
	payload = skb_put(packet, payload_size);
	real_size = krlnc_encoder_write_payload(encoder, payload);
	if (payload_size > packet->len) {
		skb_trim(packet, payload_size - real_size);
	}
	skb_trim_zeros(packet);

	return 0;
}

int get_kodo_codec(krlnc_coding_vector_format *codec, const char *name)
{
	if (name == NULL || !strcmp(name, "") || !strcmp(name, "full_vector")) {
		*codec = krlnc_full_vector;
	} else if (!strcmp(name, "seed")) {
		*codec = krlnc_seed;
	} else if (!strcmp(name, "sparse_seed")) {
		*codec = krlnc_sparse_seed;
	} else {
		return -1;
	}
	return 0;
}

int get_kodo_field(krlnc_finite_field *field, const char *name)
{
	if (name == NULL || !strcmp(name, "") || !strcmp(name, "binary")) {
		*field = krlnc_binary;
	} else if (!strcmp(name, "binary4")) {
		*field = krlnc_binary4;
	} else if (!strcmp(name, "binary8")) {
		*field = krlnc_binary8;
	} else {
		return -1;
	}
	return 0;
}

int get_kodo_options(krlnc_coding_vector_format *codec, krlnc_finite_field *field, uint32_t
		*symbols, uint32_t *symbol_size, void *context,
		nck_opt_getter get_opt)
{
	const char *value;

	value = get_opt(context, "codec");
	if (get_kodo_codec(codec, value)) {
		fprintf(stderr, "Unknown codec: %s\n", value);
		return -1;
	}

	value = get_opt(context, "field");
	if (get_kodo_field(field, value)) {
		fprintf(stderr, "Unknown field: %s\n", value);
		return -1;
	}

	value = get_opt(context, "symbols");
	if (nck_parse_u32(symbols, value)) {
		fprintf(stderr, "Invalid symbol count: %s\n", value);
		return -1;
	}

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(symbol_size, value)) {
		fprintf(stderr, "Invalid symbol size: %s\n", value);
		return -1;
	}

	return 0;
}


int get_kodo_enc_factory(krlnc_encoder_factory_t *factory,
		void *context, nck_opt_getter get_opt)
{
	uint32_t symbols = 64, symbol_size = 1400;
	krlnc_finite_field field = krlnc_binary8;
	krlnc_coding_vector_format codec = krlnc_full_vector;

	if (get_kodo_options(&codec, &field, &symbols, &symbol_size, context, get_opt)) {
		return -1;
	}

	*factory = krlnc_new_encoder_factory(field, symbols, symbol_size);
	krlnc_encoder_factory_set_coding_vector_format(*factory, codec);
	return 0;
}

int get_kodo_dec_factory(krlnc_decoder_factory_t *factory,
		void *context, nck_opt_getter get_opt)
{
	uint32_t symbols = 64, symbol_size = 1400;
	krlnc_finite_field field = krlnc_binary8;
	krlnc_coding_vector_format codec = krlnc_full_vector;

	if (get_kodo_options(&codec, &field, &symbols, &symbol_size, context, get_opt)) {
		return -1;
	}

	*factory = krlnc_new_decoder_factory(field, symbols, symbol_size);
	krlnc_decoder_factory_set_coding_vector_format(*factory, codec);
	return 0;
}

