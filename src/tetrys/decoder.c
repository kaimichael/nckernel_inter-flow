#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/types.h>

#include <nckernel/tetrys.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/trace.h>

#include <list.h>

#include "../private.h"
#include "../util/finite_field.h"

#define for_each_symbol(s, l) list_for_each_entry((s), (l), list)
#define for_each_coeff(c, l) list_for_each_entry((c), (l), list)
#define first_symbol(l) list_first_entry((l), struct symbol, list)
#define next_symbol(s) list_first_entry(&(s)->list, struct symbol, list)
#define prev_symbol(s) list_entry((s)->list.prev, struct symbol, list)
#define first_coeff(s) list_first_entry(&(s)->coefficients.list, struct coefficient, list)
#define next_coeff(c) list_first_entry(&(c)->list, struct coefficient, list)
#define is_decoded(s) list_empty(&(s)->coefficients.list)


struct coefficient {
	uint32_t id;
	uint8_t value;

	struct list_head list;
};

struct symbol {
	struct list_head list;

	struct coefficient coefficients;

	uint8_t data[];
};

struct nck_tetrys_dec {
	size_t source_size, coded_size, feedback_size;
	size_t max_window_size;

	struct nck_trigger on_source_ready;
	struct nck_trigger on_feedback_ready;

	int has_feedback;

	struct list_head symbols;
	struct symbol *next;
	uint32_t next_id;	/* contains the next symbol expected to be decoded */
	uint32_t oldest_id;
	uint32_t newest_id;
};


NCK_DECODER_IMPL(nck_tetrys, NULL, NULL, NULL)

EXPORT
struct nck_tetrys_dec *nck_tetrys_dec(size_t symbol_size, int window_size)
{
	struct nck_tetrys_dec *result;

	binary8_init();

	result = malloc(sizeof(*result) + symbol_size);
	memset(result, 0, sizeof(*result) + symbol_size);
	nck_trigger_init(&result->on_source_ready);
	nck_trigger_init(&result->on_feedback_ready);
	result->source_size = symbol_size;
	result->max_window_size = window_size;
	result->coded_size = symbol_size + 5*window_size + 9;
	result->feedback_size = 5 + 4*window_size + 4;

	result->has_feedback = 0;
	INIT_LIST_HEAD(&result->symbols);
	result->next = NULL;
	result->next_id = 0;

	return result;
}

static void add_coded(struct nck_tetrys_dec *decoder, struct symbol *symbol);

static void print_symbol(FILE *file, struct symbol *s, size_t length)
{
	struct sk_buff packet;
	struct coefficient *c;
	fprintf(file, "symbol %p\n", (void*)s);
	fprintf(file, "  id=%u value=0x%02x\n", s->coefficients.id, s->coefficients.value);
	for_each_coeff(c, &s->coefficients.list) {
		fprintf(file, "  id=%u value=0x%02x\n", c->id, c->value);
	}

	skb_new(&packet, s->data, length);
	skb_print(file, &packet);
}

static void symbol_free(struct symbol *s)
{
	struct coefficient *c;

	while (!list_empty(&s->coefficients.list)) {
		c = next_coeff(&s->coefficients);
		list_del(&c->list);
		free(c);
	}

	free(s);
}

EXPORT
void nck_tetrys_dec_free(struct nck_tetrys_dec *decoder)
{
	struct symbol *s;
	while (!list_empty(&decoder->symbols)) {
		s = first_symbol(&decoder->symbols);
		list_del(&s->list);
		symbol_free(s);
	}
	free(decoder);
}

EXPORT
int nck_tetrys_dec_has_source(struct nck_tetrys_dec *decoder)
{
	if (decoder->next == NULL)
		return 0;

	if (!is_decoded(decoder->next))
		return 0;

	return 1;
}

EXPORT
int nck_tetrys_dec_complete(struct nck_tetrys_dec *decoder)
{
	UNUSED(decoder);
	return 0;
}

EXPORT
void nck_tetrys_dec_flush_source(struct nck_tetrys_dec *decoder)
{
	UNUSED(decoder);
}

static void multiply_subtract(struct symbol *dst, struct coefficient *coeff, uint8_t factor, struct list_head *src)
{
	struct coefficient *src_coeff, *new_coeff, *next;

	for_each_coeff (src_coeff, src) {
		while (&coeff->list != &dst->coefficients.list && (int)(coeff->id - src_coeff->id) < 0) {
			// the coefficient exists in dst but not in src, we can leave it unchanged
			// but move forward
			coeff = next_coeff(coeff);
		}

		if (&coeff->list != &dst->coefficients.list && coeff->id == src_coeff->id) {
			// if the coefficient exists in both lists we update its value
			coeff->value = binary8_subtract(coeff->value, binary8_multiply(src_coeff->value, factor));
			if (coeff->value == 0) {
				next = next_coeff(coeff);

				list_del(&coeff->list);
				free(coeff);

				coeff = next;
			}
		} else {
			// otherwise we insert it before the current pointer
			new_coeff = malloc(sizeof(struct coefficient));
			new_coeff->id = src_coeff->id;
			new_coeff->value = binary8_subtract(0, binary8_multiply(src_coeff->value, factor));
			list_add_before(&new_coeff->list, &coeff->list);
		}
	}
}

/**
 * eliminate_from_symbols - use the uncoded symbol to eliminate the coefficient from the
 * other symbols. The symbol must already be inserted in the symbol list of the decoder.
 */
static void eliminate_from_symbols(struct nck_tetrys_dec *decoder, struct symbol *src)
{
	struct symbol *dst;
	struct coefficient *coeff;
	uint8_t factor;

	// because the symbol is in the list, we can just iterate backwards to
	// find all symbols which might have this coefficient

	for (dst = prev_symbol(src); &dst->list != &decoder->symbols; dst = prev_symbol(dst)) {
		if (is_decoded(dst)) {
			// nothing to do
			continue;
		}

		assert((int)(dst->coefficients.id - src->coefficients.id) < 0);

		// search for the coefficient
		for_each_coeff(coeff, &dst->coefficients.list) {
			if (coeff->id == src->coefficients.id) {
				// if we found the coefficient, we eliminate it
				factor = coeff->value;
				assert(binary8_subtract(coeff->value, binary8_multiply(src->coefficients.value, factor)) == 0);

				// update the remaining coefficient vector
				if (!is_decoded(src)) {
					multiply_subtract(dst, coeff, factor, &src->coefficients.list);
				}

				list_del(&coeff->list);
				free(coeff);

				// update the payload
				binary8_region_multiply_subtract(dst->data, src->data, factor, decoder->source_size);

				// nothing more to do for this symbol
				break;
			}
		}
	}
}

/**
 * insert - add a symbol to the sorted list
 * @symbols: sorted list to insert into
 * @symbol: new symbol for the list
 */
static void insert(struct nck_tetrys_dec *decoder, struct symbol *symbol)
{
	struct symbol *s, *replace = NULL;
	struct list_head *tail = &decoder->symbols;

	for_each_symbol(s, &decoder->symbols) {
		if (s->coefficients.id == symbol->coefficients.id) {
			// this should not happen for coded symbols, because the coefficient should
			// already be eliminated
			assert(is_decoded(symbol));

			if (is_decoded(s)) {
				// if both are decoded they should be equal
				assert(memcmp(s->data, symbol->data, decoder->source_size) == 0);
				// but we do delete the symbol and return
				symbol_free(symbol);
				return;
			} else {
				// if the stored one is coded, we replace it with our uncoded one and
				// re-add the coded symbol
				tail = &s->list;
				replace = s;
				break;
			}
		}

		if ((int)(s->coefficients.id - symbol->coefficients.id) > 0) {
			// we found the position to insert
			tail = &s->list;
			break;
		}
	}

	list_add_before(&symbol->list, tail);

	if (symbol->coefficients.id == decoder->next_id) {
		decoder->next = symbol;
	}

	if (replace == NULL) {
		// we did not replace but did a real addition
		eliminate_from_symbols(decoder, symbol);
	} else {
		// we replace an existing symbol
		list_del(&replace->list);
		// elimination is not necessary, because this was done with the replaced symbol
		// but we re-add the replaced symbol
		add_coded(decoder, replace);
	}
}

/**
 * evict_outdated_symbols - remove symbols which are older then the oldest_id and out of range
 * @decoder: decoder structure on which to evict
 */
static void evict_outdated_symbols(struct nck_tetrys_dec *decoder)
{
	struct symbol *s, *next;

	list_for_each_entry_safe(s, next, &decoder->symbols, list) {
		if ((int)(s->coefficients.id - decoder->oldest_id) >= 0) {
			// after this all symbols might still be needed

			// also if the next_id is before oldest_id we might need to move it forward
			if ((int)(decoder->next_id - decoder->oldest_id) < 0) {
				if (decoder->next == NULL) {
					// if we did not find the next symbol yet then the next id
					// should be the oldest id
					decoder->next_id = decoder->oldest_id;
				}

				if (decoder->next_id == s->coefficients.id) {
					decoder->next = s;
				}
			}

			break;
		}

		if ((int)(decoder->next_id - s->coefficients.id) <= 0) {
			// we should keep this symbol because it was not yet output
			if (decoder->next == NULL) {
				// we can't expect to receive older symbols, so we just continue with what we have
				decoder->next = s;
				decoder->next_id = s->coefficients.id;
			}

			if (is_decoded(s)) {
				// if it is decoded we can actually keep it
				continue;
			}

			if ((int)(decoder->oldest_id - first_coeff(s)->id) <= 0) {
				// the symbol still can be decoded, so we keep it
				continue;
			}
		}

		if (s == decoder->next) {
			// if we delete the 'next' symbol we have to advance forward
			decoder->next = NULL;
			decoder->next_id += 1;
		}

		list_del(&s->list);
		symbol_free(s);
		continue;
	}
}

/**
 * normalize - divide by the value of the first coefficient
 * @symbol: symbol to normalize
 * @source_size: size of the payload
 */
static void normalize(struct symbol *symbol, size_t source_size)
{
	uint8_t factor;
	struct coefficient *coeff;

	// normalize on first coefficient
	factor = binary8_invert(symbol->coefficients.value);
	assert(binary8_multiply(symbol->coefficients.value, factor) == 1);
	symbol->coefficients.value = 1;

	// multiply the payload
	binary8_region_multiply(symbol->data, factor, source_size);

	// multiply the coefficients
	for_each_coeff(coeff, &symbol->coefficients.list) {
		coeff->value = binary8_multiply(coeff->value, factor);
	}
}

/**
 * trim_zero_coeff - remove all coefficients with value zero
 */
static void trim_zero_coeff(struct symbol *symbol)
{
	struct coefficient *coeff, *next;
	// start with the leading coefficient
	while (!is_decoded(symbol) && symbol->coefficients.value == 0) {
		coeff = first_coeff(symbol);
		symbol->coefficients.id = coeff->id;
		symbol->coefficients.value = coeff->value;
		list_del(&coeff->list);
		free(coeff);
	}

	// iterate over the remaining coefficients
	list_for_each_entry_safe(coeff, next, &symbol->coefficients.list, list) {
		if (coeff->value == 0) {
			list_del(&coeff->list);
			free(coeff);
		}
	}
}

/**
 * eliminate_with_symbols - use the symbol list to eliminate the coefficients
 * @dst: symbol where the coefficients will be eliminated
 * @symbols: list of symbols to use for the elimination
 * @source_size: size of the payload
 */
static void eliminate_with_symbols(struct symbol *dst, struct list_head *symbols, size_t source_size)
{
	struct symbol *src;
	uint8_t factor;
	struct coefficient *coeff;

	for_each_symbol (src, symbols) {
		if (is_decoded(dst) && dst->coefficients.value == 0) {
			// if it is already decoded there is nothing to do
			return;
		}

		if ((int)(src->coefficients.id - dst->coefficients.id) < 0) {
			// src symbol cannot be used for elimination
			continue;
		} else if (src->coefficients.id == dst->coefficients.id) {
			factor = binary8_divide(dst->coefficients.value, src->coefficients.value);
			assert(binary8_subtract(dst->coefficients.value, binary8_multiply(src->coefficients.value, factor)) == 0);

			// update the payload
			binary8_region_multiply_subtract(dst->data, src->data, factor, source_size);

			// update the remaining coefficient vector
			if (!is_decoded(src)) {
				multiply_subtract(dst, first_coeff(dst), factor, &src->coefficients.list);
			}

			if (is_decoded(dst)) {
				// we have a linear dependent symbol
				dst->coefficients.value = 0;
				return;
			}

			coeff = first_coeff(dst);
			dst->coefficients.id = coeff->id;
			dst->coefficients.value = coeff->value;

			list_del(&coeff->list);
			free(coeff);
		} else {
			for_each_coeff (coeff, &dst->coefficients.list) {
				if ((int)(coeff->id - src->coefficients.id) > 0) {
					// no need for elimination
					break;
				} else if (coeff->id == src->coefficients.id) {
					// if we found the coefficient, we eliminate it
					factor = coeff->value;
					assert(binary8_subtract(coeff->value, binary8_multiply(src->coefficients.value, factor)) == 0);

					// update the remaining coefficient vector
					if (!is_decoded(src)) {
						multiply_subtract(dst, coeff, factor, &src->coefficients.list);
					}

					list_del(&coeff->list);
					free(coeff);

					// update the payload
					binary8_region_multiply_subtract(dst->data, src->data, factor, source_size);

					// nothing more to do for this symbol
					break;
				}
			}
		}
	}

	trim_zero_coeff(dst);

	if (dst->coefficients.value == 0) {
		assert(is_decoded(dst));
	} else {
		normalize(dst, source_size);
	}
}

static void add_coded(struct nck_tetrys_dec *decoder, struct symbol *symbol)
{
	trim_zero_coeff(symbol);
	if (symbol->coefficients.value == 0) {
		assert(is_decoded(symbol));
		for (size_t i = 0; i < decoder->source_size; ++i) {
			assert(symbol->data[i] == 0);
		}
		symbol_free(symbol);
		return;
	}

	// we try to eliminate coefficients using the existing symbols
	eliminate_with_symbols(symbol, &decoder->symbols, decoder->source_size);

	if (symbol->coefficients.value == 0) {
		// linear dependent symbol
		assert(is_decoded(symbol));
		for (size_t i = 0; i < decoder->source_size; ++i) {
			assert(symbol->data[i] == 0);
		}
		symbol_free(symbol);
		return;
	}

	// save the symbol into our list
	insert(decoder, symbol);
}

EXPORT
int nck_tetrys_dec_put_coded(struct nck_tetrys_dec *decoder, struct sk_buff *packet)
{
	struct symbol *symbol;
	struct coefficient *coeff = NULL;
	uint8_t type;
	uint32_t id;
	uint32_t count;

	type = skb_pull_u8(packet);
	id = skb_pull_u32(packet);
	if (type == 0) {
		// source packet
		symbol = malloc(sizeof(*symbol) + decoder->source_size);
		memset(symbol->data, 0, decoder->source_size);

		// copy payload
		memcpy(symbol->data, packet->data, packet->len);

		// initialize coefficients
		symbol->coefficients.id = id;
		symbol->coefficients.value = 1;
		INIT_LIST_HEAD(&symbol->coefficients.list);

		if (id > decoder->newest_id) {
			decoder->newest_id = id;
		}

		insert(decoder, symbol);
	} else if (type == 1) {
		// coded packet
		count = skb_pull_u32(packet);
		assert(count > 0);

		symbol = malloc(sizeof(*symbol) + decoder->source_size);
		memset(symbol->data, 0, decoder->source_size);

		// initialize first coefficient
		symbol->coefficients.id = skb_pull_u32(packet);
		symbol->coefficients.value = skb_pull_u8(packet);
		INIT_LIST_HEAD(&symbol->coefficients.list);

		decoder->oldest_id = symbol->coefficients.id;

		// initialize following coefficients
		// read the rest of the coefficients and put them into the linked list
		for (--count; count > 0; --count) {
			coeff = malloc(sizeof(*coeff));
			coeff->id = skb_pull_u32(packet);
			coeff->value = skb_pull_u8(packet);
			list_add_tail(&coeff->list, &symbol->coefficients.list);
		}

		if (coeff && coeff->id > decoder->newest_id)
			decoder->newest_id = coeff->id;

		// copy payload
		assert(packet->len <= decoder->source_size);
		memcpy(symbol->data, packet->data, packet->len);

		// we remove all outdated symbols to reduce the complexity
		evict_outdated_symbols(decoder);

		add_coded(decoder, symbol);
	} else {
		return -1;
	}

	decoder->has_feedback = 1;
	nck_trigger_call(&decoder->on_feedback_ready);

	if (_has_source(decoder)) {
		nck_trigger_call(&decoder->on_source_ready);
	}

	return 0;
}

EXPORT
int nck_tetrys_dec_get_source(struct nck_tetrys_dec *decoder, struct sk_buff *packet)
{
	struct symbol *next;

	if (decoder->next == NULL)
		return -1;

	if (!is_decoded(decoder->next))
		return -1;

	next = decoder->next;

	assert(next->coefficients.id == decoder->next_id);
	assert(next->coefficients.value == 1);

	uint8_t *payload = skb_put(packet, decoder->source_size);
	memcpy(payload, next->data, decoder->source_size);

	decoder->next_id += 1;
	decoder->next = NULL;

	// find the next id and symbol
	// this is more complicated than it seems, try to make it simple
	if ((int)(decoder->next_id - decoder->oldest_id) < 0) {
		// if our pointer is before the oldest symbol,
		// we just call evict_outdated_symbols, it should take care of the details
		evict_outdated_symbols(decoder);
	} else if (next->list.next != &decoder->symbols) {
		// otherwise we check if the next symbol matches
		next = next_symbol(next);
		if (next->coefficients.id == decoder->next_id) {
			decoder->next = next;
		}
	}

	return 0;
}

EXPORT
int nck_tetrys_dec_get_feedback(struct nck_tetrys_dec *decoder, struct sk_buff *packet)
{
	struct symbol *symbol;
	uint32_t missing;

	assert(decoder->has_feedback);
	if (!decoder->has_feedback) {
		return -1;
	}
	decoder->has_feedback = 0;

	skb_reserve(packet, 5);

	if (!list_empty(&decoder->symbols)) {
		missing = decoder->next_id;

		for_each_symbol(symbol, &decoder->symbols) {
			if ((int)(symbol->coefficients.id - decoder->next_id) < 0) {
				// we report nothing before decoder->next_id
				continue;
			}

			while (missing != symbol->coefficients.id) {
				assert(packet->len + 13 <= decoder->feedback_size);
				skb_put_u32(packet, missing++);
			}

			assert(missing == symbol->coefficients.id);
			missing++;
		}
	}

	assert(packet->len + 5 <= decoder->feedback_size);
	skb_put_u32(packet, missing);
	skb_push_u8(packet, 2);

	return 0;
}

EXPORT
int nck_tetrys_dec_has_feedback(struct nck_tetrys_dec *decoder)
{
	return decoder->has_feedback;
}

