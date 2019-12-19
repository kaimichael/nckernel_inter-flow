#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/nckernel.h>
#include <nckernel/interflowsw.h>
#include <nckernel/trace.h>

#include "../private.h"
#include "../config.h"

EXPORT
int nck_interflow_sw_enc_set_option(struct nck_interflow_sw_enc *encoder, const char *name, const char *value)
{
	nck_trace(encoder, "configure %s = %s\n", name, value);
	if (!strcmp("redundancy", name)) {
		int32_t redundancy = 0;
		if (nck_parse_s32(&redundancy, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_redundancy(encoder, redundancy);
	} else if (!strcmp("systematic", name)) {
		uint32_t systematic = 0;
		if (nck_parse_u32(&systematic, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_systematic_phase(encoder, systematic);
	} else if (!strcmp("coded", name)) {
		uint32_t coded = 1;
		if (nck_parse_u32(&coded, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_coded_phase(encoder, coded);
	} else if (!strcmp("forward_code_window", name)) {
		uint32_t forward_code_window = 1;
		if (nck_parse_u32(&forward_code_window, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_forward_code_window(encoder, forward_code_window);
	} else if (!strcmp("feedback", name)) {
		uint32_t period = 0;
		if (nck_parse_u32(&period, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_feedback_period(encoder, period);
	} else if (!strcmp("coded_retransmissions", name)) {
		uint32_t coded_retrans = 1;
		if (nck_parse_u32(&coded_retrans, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_coded_retransmissions(encoder, coded_retrans);
	} else if (!strcmp("memory", name)) {
		uint32_t memory = 0;
		if (nck_parse_u32(&memory, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_memory(encoder, memory);
	} else if (!strcmp("tx_attempts", name)) {
		uint8_t tx_attempts = 0;
		if (nck_parse_u8(&tx_attempts, value)) {
			return EINVAL;
		}

		/* at least one attempt is required */
		if (tx_attempts == 0)
			return EINVAL;

		nck_interflow_sw_enc_set_tx_attempts(encoder, tx_attempts);
	}	else if (!strcmp("node_id", name)) {
			uint32_t node_id = 0;
			if (nck_parse_u32(&node_id, value)) {
				return EINVAL;
			}

		nck_interflow_sw_enc_set_node_id(encoder, node_id);

	} else if (!strcmp("n_nodes", name)) {
			uint32_t n_nodes = 1;
			if (nck_parse_u32(&n_nodes, value)) {
				return EINVAL;
			}

		nck_interflow_sw_enc_set_n_nodes(encoder, n_nodes);


	} else if (!strcmp("sequence", name)) {
		uint32_t sequence = 0;
		if (nck_parse_u32(&sequence, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_sequence(encoder, sequence);
	} else if (!strcmp("feedback_only_on_repair", name)) {
		uint32_t feedback_only_on_repair = 0;
		if (nck_parse_u32(&feedback_only_on_repair, value)) {
			return EINVAL;
		}

		nck_interflow_sw_enc_set_feedback_only_on_repair(encoder, feedback_only_on_repair);


} else {
		return ENOTSUP;
	}

	return 0;
}

EXPORT
int nck_interflow_sw_rec_set_option(struct nck_interflow_sw_rec *recoder, const char *name, const char *value)
{
	if (!strcmp("redundancy", name)) {
		int32_t redundancy = 0;
		if (nck_parse_s32(&redundancy, value)) {
			return EINVAL;
		}

		nck_interflow_sw_rec_set_redundancy(recoder, redundancy);
	} else if (!strcmp("feedback", name)) {
		uint32_t enable = 0;
		if (nck_parse_u32(&enable, value)) {
			return EINVAL;
		}

		nck_interflow_sw_rec_set_feedback(recoder, enable);
	} else if (!strcmp("tx_attempts", name)) {
		uint8_t tx_attempts = 0;
		if (nck_parse_u8(&tx_attempts, value)) {
			return EINVAL;
		}

		nck_interflow_sw_rec_set_tx_attempts(recoder, tx_attempts);
	} else if (!strcmp("forward_code_window", name)) {
		uint32_t forward_code_window = 1;
		if (nck_parse_u32(&forward_code_window, value)) {
			return EINVAL;
		}
		nck_interflow_sw_rec_set_forward_code_window(recoder, forward_code_window);
	} else {
		return ENOTSUP;
	}
	return 0;
}

EXPORT
int nck_interflow_sw_dec_set_option(struct nck_interflow_sw_dec *decoder, const char *name, const char *value)
{
	if (!strcmp("sequence", name)) {
		uint32_t sequence = 0;
		if (nck_parse_u32(&sequence, value)) {
			return EINVAL;
		}

		nck_interflow_sw_dec_set_sequence(decoder, sequence);
	} else if (!strcmp("tx_attempts", name)) {
		uint8_t tx_attempts = 0;
		if (nck_parse_u8(&tx_attempts, value)) {
			return EINVAL;
		}

		nck_interflow_sw_dec_set_tx_attempts(decoder, tx_attempts);
	} else if (!strcmp("feedback", name)) {
		uint32_t enable = 0;
		if (nck_parse_u32(&enable, value)) {
			return EINVAL;
		}

		nck_interflow_sw_dec_set_feedback(decoder, enable);
	} else {
		return ENOTSUP;
	}
	return 0;
}

EXPORT
int nck_interflow_sw_create_enc(struct nck_encoder *encoder, struct nck_timer *timer,
		void *context, nck_opt_getter get_opt)
{
	const char *value;
	uint32_t symbols = 16, symbol_size = 1400;
	struct timeval timeout = { 0, 100000 };
	struct nck_interflow_sw_enc *enc;

	value = get_opt(context, "symbols");
	if (nck_parse_u32(&symbols, value)) {
		fprintf(stderr, "Invalid symbol count: %s\n", value);
		return -1;
	}

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol size: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (!timer) {
		assert(value == NULL);
		timerclear(&timeout);
	} else {
		if (nck_parse_timeval(&timeout, value)) {
			fprintf(stderr, "Invalid timeout: %s\n", value);
			return -1;
		}
	}

	enc = nck_interflow_sw_enc(symbols, symbol_size, timer, &timeout);

	value = get_opt(context, "forward_code_window");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "forward_code_window", value);
	}

	value = get_opt(context, "redundancy");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "redundancy", value);
	}

	value = get_opt(context, "systematic");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "systematic", value);
	}

	value = get_opt(context, "coded");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "coded", value);
	}

	value = get_opt(context, "coded_retransmissions");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "coded_retransmissions", value);
	}

	value = get_opt(context, "feedback");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "feedback", value);
	}

	value = get_opt(context, "memory");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "memory", value);
	}

	value = get_opt(context, "tx_attempts");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "tx_attempts", value);
	}

	value = get_opt(context, "n_nodes");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "n_nodes", value);
	}

	value = get_opt(context, "node_id");
	if (value) {
		nck_interflow_sw_enc_set_option(enc, "node_id", value);
	}

	nck_interflow_sw_enc_api(encoder, enc);
	return 0;
}

EXPORT
int nck_interflow_sw_create_dec(struct nck_decoder *decoder, struct nck_timer *timer,
		void *context, nck_opt_getter get_opt)
{
	uint32_t symbols = 16, symbol_size = 1400;
	struct timeval timeout = { 0, 500000 };
	struct timeval fb_timeout = { 0, 0 };
	struct nck_interflow_sw_dec *dec;
	const char *value;
	char matrix_form[20] = "";

	UNUSED(timer);

	value = get_opt(context, "symbols");
	if (nck_parse_u32(&symbols, value)) {
		fprintf(stderr, "Invalid symbol count: %s\n", value);
		return -1;
	}

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol size: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (!timer) {
		assert(value == NULL);
		timerclear(&timeout);
	} else {
		if (nck_parse_timeval(&timeout, value)) {
			fprintf(stderr, "Invalid timeout: %s\n", value);
			return -1;
		}
	}

	value = get_opt(context, "matrix_form");
	if (value) {
		strncpy(matrix_form, value, sizeof(matrix_form));
		matrix_form[sizeof(matrix_form)-1] = 0;
	}
	dec = nck_interflow_sw_dec(symbols, symbol_size, timer, &timeout, matrix_form);

	value = get_opt(context, "sequence");
	if (value) {
		nck_interflow_sw_dec_set_option(dec, "sequence", value);
	}

	value = get_opt(context, "feedback");
	if (value) {
		nck_interflow_sw_dec_set_option(dec, "feedback", value);
	}

	value = get_opt(context, "tx_attempts");
	if (value) {
		nck_interflow_sw_dec_set_option(dec, "tx_attempts", value);
	}

	value = get_opt(context, "fb_timeout");
	if (!timer) {
		assert(value == NULL);
		timerclear(&fb_timeout);
	} else {
		if (nck_parse_timeval(&fb_timeout, value)) {
			fprintf(stderr, "Invalid fb_timeout: %s\n", value);
			return -1;
		}
		nck_interflow_sw_dec_set_fb_timeout(dec, &fb_timeout);
	}

	nck_interflow_sw_dec_api(decoder, dec);
	return 0;
}

EXPORT
int nck_interflow_sw_create_rec(struct nck_recoder *recoder, struct nck_timer *timer,
		void *context, nck_opt_getter get_opt)
{
	uint32_t symbols = 16, symbol_size = 1400;
	struct timeval timeout = { 0, 500000 };
	const char *value;
	struct nck_interflow_sw_rec *rec;

	UNUSED(timer);

	value = get_opt(context, "symbols");
	if (nck_parse_u32(&symbols, value)) {
		fprintf(stderr, "Invalid symbol count: %s\n", value);
		return -1;
	}

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol size: %s\n", value);
		return -1;
	}

	value = get_opt(context, "timeout");
	if (nck_parse_timeval(&timeout, value)) {
		fprintf(stderr, "Invalid timeout: %s\n", value);
		return -1;
	}

	rec = nck_interflow_sw_rec(symbols, symbol_size, timer, &timeout);

	value = get_opt(context, "feedback");
	if (value) {
		nck_interflow_sw_rec_set_option(rec, "feedback", value);
	}

	value = get_opt(context, "redundancy");
	if (value) {
		nck_interflow_sw_rec_set_option(rec, "redundancy", value);
	}

	value = get_opt(context, "tx_attempts");
	if (value) {
		nck_interflow_sw_rec_set_option(rec, "tx_attempts", value);
	}

	value = get_opt(context, "forward_code_window");
	if (value) {
		nck_interflow_sw_rec_set_option(rec, "forward_code_window", value);
	}

	nck_interflow_sw_rec_api(recoder, rec);
	return 0;
}
