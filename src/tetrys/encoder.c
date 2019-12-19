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
#include <nckernel/timer.h>
#include <nckernel/trace.h>

#include <list.h>

#include "../private.h"
#include "../util/rate.h"
#include "../util/finite_field.h"

struct source_symbol
{
	struct list_head list;

	uint32_t id;
	size_t len;
	uint8_t data[];
};

struct nck_tetrys_enc {
	size_t source_size, coded_size, feedback_size;

	struct nck_trigger on_coded_ready;

	struct rate_control rc;
	unsigned int seed;

	int max_window_size;
	int window_size;

	struct list_head window;
	struct source_symbol *next;

	uint32_t coded_id;
	uint32_t source_id;

	struct timeval timeout;
	struct nck_timer_entry *timeout_handle;
};

NCK_ENCODER_IMPL(nck_tetrys, NULL, NULL, NULL)

static void encoder_timeout_flush(struct nck_timer_entry *entry, void *context, int success)
{
	UNUSED(entry);

	if (success) {
		_flush_coded(context);
	}
}

EXPORT
struct nck_tetrys_enc *nck_tetrys_enc(size_t symbol_size, int window_size, struct nck_timer *timer, const struct timeval *timeout)
{
	struct nck_tetrys_enc *result;

	binary8_init();

	result = malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	nck_trigger_init(&result->on_coded_ready);
	result->seed = rand();

	rate_control_dual_init(&result->rc, window_size/2, 1);

	result->max_window_size = window_size;
	result->window_size = 0;
	INIT_LIST_HEAD(&result->window);
	result->next = NULL;

	result->coded_id = 0;

	result->source_size = symbol_size;
	result->coded_size = symbol_size + 5*result->max_window_size + 9;
	result->feedback_size = 5 + 4*result->max_window_size + 4;

	if (timeout && timerisset(timeout)) {
		assert(timer != NULL);
		result->timeout = *timeout;
		result->timeout_handle = nck_timer_add(timer, NULL, result, encoder_timeout_flush);
	}

	return result;
}

EXPORT
void nck_tetrys_enc_set_systematic_phase(struct nck_tetrys_enc *encoder, uint32_t phase_length)
{
	rate_control_dual_change(&encoder->rc, phase_length, encoder->rc.dual.repair_phase);
}

EXPORT
void nck_tetrys_enc_set_coded_phase(struct nck_tetrys_enc *encoder, uint32_t phase_length)
{
	rate_control_dual_change(&encoder->rc, encoder->rc.dual.source_phase, phase_length);
}

EXPORT
void nck_tetrys_enc_free(struct nck_tetrys_enc *encoder)
{
	struct source_symbol *symbol;

	if (encoder->timeout_handle) {
		nck_timer_cancel(encoder->timeout_handle);
		nck_timer_free(encoder->timeout_handle);
	}
	while (!list_empty(&encoder->window)) {
		symbol = list_first_entry(&encoder->window, struct source_symbol, list);

		list_del(&symbol->list);
		free(symbol);
		--encoder->window_size;
	}
	assert(encoder->window_size == 0);
	free(encoder);
}

EXPORT
int nck_tetrys_enc_has_coded(struct nck_tetrys_enc *encoder)
{
	return !list_empty(&encoder->window) && (encoder->next != NULL || (rate_control_next_repair(&encoder->rc, 0 /* TODO */)));
}

EXPORT
int nck_tetrys_enc_full(struct nck_tetrys_enc *encoder)
{
	return encoder->window_size >= encoder->max_window_size;
}

EXPORT
int nck_tetrys_enc_complete(struct nck_tetrys_enc *encoder)
{
	return list_empty(&encoder->window);
}

EXPORT
void nck_tetrys_enc_flush_coded(struct nck_tetrys_enc *encoder)
{
	if (!list_empty(&encoder->window)) {
		rate_control_dual_reset(&encoder->rc, 1, encoder->window_size);
		nck_trigger_call(&encoder->on_coded_ready);
	}
}

EXPORT
int nck_tetrys_enc_put_source(struct nck_tetrys_enc *encoder, struct sk_buff *packet)
{
	struct source_symbol *symbol, *evict;

	assert(packet->len <= encoder->source_size);

	symbol = malloc(sizeof(*symbol) + packet->len);
	symbol->id = encoder->source_id++;
	symbol->len = packet->len;
	memcpy(symbol->data, packet->data, packet->len);

	list_add_tail(&symbol->list, &encoder->window);
	if (encoder->next == NULL) {
		encoder->next = symbol;
	}

	encoder->window_size++;
	while (encoder->window_size > encoder->max_window_size) {
		evict = list_first_entry(&encoder->window, typeof(*evict), list);
		if (encoder->next == evict) {
			if (evict->list.next == &encoder->window) {
				encoder->next = NULL;
			} else {
				encoder->next = list_first_entry(&evict->list, typeof(*encoder->next), list);
			}
		}

		list_del(&evict->list);
		free(evict);
		--encoder->window_size;
	}

	if (encoder->timeout_handle) {
		// we definitelly have something to send now, so we cancel the timeout
		nck_timer_cancel(encoder->timeout_handle);
	}

	nck_trigger_call(&encoder->on_coded_ready);

	return 0;
}

EXPORT
int nck_tetrys_enc_get_coded(struct nck_tetrys_enc *encoder, struct sk_buff *packet)
{
	struct source_symbol *symbol;
	uint8_t payload[encoder->source_size], *pos;
	uint8_t coeff;
	size_t len;
	uint32_t count;

	assert(nck_tetrys_enc_has_coded(encoder));
	if (!nck_tetrys_enc_has_coded(encoder))
		return -1;

	if (rate_control_step(&encoder->rc, 0 /* TODO */) || encoder->next == NULL) {
		assert(!list_empty(&encoder->window));

		// reserve space for the flag, id and window size
		skb_reserve(packet, 9);

		// produce a coded packet
		memset(payload, 0, encoder->source_size);
		len = 0;
		count = 0;

		list_for_each_entry(symbol, &encoder->window, list) {
			coeff = rand_r(&encoder->seed)&0xff;
			binary8_region_multiply_add(payload, symbol->data, coeff, symbol->len);
			if (symbol->len > len) {
				len = symbol->len;
			}
			skb_put_u32(packet, symbol->id);
			skb_put_u8(packet, coeff);
			++count;
		}

		assert(count);
		assert(len <= encoder->source_size);

		pos = skb_put(packet, encoder->source_size);
		memcpy(pos, payload, encoder->source_size);

		skb_trim(packet, encoder->source_size - len);

		skb_push_u32(packet, count);
		skb_push_u32(packet, encoder->coded_id++);
		skb_push_u8(packet, 1);
	} else {
		/* produce a systematic packet */
		symbol = encoder->next;

		skb_reserve(packet, 5);
		pos = skb_put(packet, symbol->len);
		memcpy(pos, symbol->data, symbol->len);
		skb_push_u32(packet, symbol->id);
		skb_push_u8(packet, 0);

		if (symbol->list.next == &encoder->window) {
			encoder->next = NULL;
		} else {
			encoder->next = list_entry(symbol->list.next, struct source_symbol, list);
		}
	}

	if (encoder->timeout_handle && !_has_coded(encoder)) {
		// We have nothing more to send, so we register a timeout.
		// The timeout should be reset if either a new source packet
		// is added or feedback arrives.
		nck_timer_rearm(encoder->timeout_handle, &encoder->timeout);
	}

	return 0;
}

EXPORT
int nck_tetrys_enc_put_feedback(struct nck_tetrys_enc *encoder, struct sk_buff *packet)
{
	uint32_t id, missing_id;
	struct source_symbol *symbol, *next;
	int packet_type;

	packet_type = skb_pull_u8(packet);
	if (packet_type != 2)
		return -1;

	assert(packet->len%sizeof(id) == 0);
	assert(packet->len != 0);	/* decoder isn't designed to send empty feedback  */

	missing_id = skb_pull_u32(packet);

	/* assuming that ids are rising in the list */
	list_for_each_entry_safe(symbol, next, &encoder->window, list) {
		assert(symbol->id <= missing_id);

		/* keep the missing symbol, and pull out the next */
		if (symbol->id >= missing_id) {
			if (packet->len <= 0)
				break;

			while (symbol->id >= missing_id && packet->len >= 4) {
				missing_id = skb_pull_u32(packet);
			}

			continue;
		}

		/* if we happen to not have sent out a packet systematically but already got
		 * it acknowledged, we still remove it but move on to the next pointer. This can
		 * happen if a feedback is received during a flush and another packet is then
		 * admitted.
		 */
		if (symbol == encoder->next) {
			if (symbol->list.next == &encoder->window) {
				encoder->next = NULL;
			} else {
				encoder->next = list_entry(symbol->list.next, struct source_symbol, list);
			}

			encoder->next = NULL;
		}

		list_del(&symbol->list);
		free(symbol);
		--encoder->window_size;
	}

	if (encoder->timeout_handle) {
		if (list_empty(&encoder->window)) {
			nck_timer_cancel(encoder->timeout_handle);
		} else if (!nck_timer_pending(encoder->timeout_handle)) {
			nck_timer_rearm(encoder->timeout_handle, &encoder->timeout);
		}
	}

	return 0;
}
