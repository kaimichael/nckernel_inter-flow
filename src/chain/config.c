#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nckernel/nckernel.h>
#include <nckernel/chain.h>

#include "../private.h"
#include "../config.h"

#define MAX_STAGES 4

struct stage_context
{
	const char *stage;
	void *parent;
	nck_opt_getter get_opt;
	const char *symbol_size;
	const char *protocol;
};

static const char *parse_stage(const char *name, unsigned int *stage)
{
	int start;

	if (sscanf(name, "stage%u%n", stage, &start) != 1) {
		return NULL;
	}

	if (name[start] == '_') {
		return &name[start+1];
	}

	return NULL;
}

static const char *stage_get_opt(void *c, const char *name)
{
	char full_name[256];
	const struct stage_context *context = (const struct stage_context *)c;

	if (!strcmp(name, "symbol_size")) {
		return context->symbol_size;
	}

	if (!strcmp(name, "protocol")) {
		return context->protocol;
	}

	snprintf(full_name, sizeof(full_name), "%s_%s", context->stage, name);
	full_name[sizeof(full_name) - 1] = 0;

	return context->get_opt(context->parent, full_name);
}

EXPORT
int nck_chain_enc_set_option(struct nck_chain_enc *encoder, const char *name, const char *value)
{
	unsigned int stage;
	name = parse_stage(name, &stage);
	if (!name) {
		return EINVAL;
	}

	return nck_chain_enc_set_stage_option(encoder, stage, name, value);
}

EXPORT
int nck_chain_dec_set_option(struct nck_chain_dec *decoder, const char *name, const char *value)
{
	unsigned int stage;
	name = parse_stage(name, &stage);
	if (!name) {
		return EINVAL;
	}

	return nck_chain_dec_set_stage_option(decoder, stage, name, value);
}

EXPORT
int nck_chain_create_enc(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	int stage;
	char stage_name[10];
	const char *value;
	uint32_t symbol_size = 1500;
	char symbol_size_str[10];
	struct nck_encoder stages[MAX_STAGES];
	struct stage_context stage_context = { stage_name, context, get_opt, symbol_size_str, NULL };

	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	for (stage = 0; stage < MAX_STAGES; ++stage) {
		sprintf(stage_name, "stage%d", stage);
		value = get_opt(context, stage_name);
		if (!value) {
			break;
		}

		snprintf(symbol_size_str, sizeof(symbol_size_str), "%u", symbol_size);
		symbol_size_str[sizeof(symbol_size_str) - 1] = 0;
		stage_context.protocol = value;

		nck_create_encoder(&stages[stage], timer, &stage_context, stage_get_opt);
		assert(symbol_size == stages[stage].source_size);
		// propagate the size of coded packets
		symbol_size = stages[stage].coded_size;
	}

	if (stage == 0) {
		// to make the tests work we create a nocode stage
		stage = 1;
		snprintf(symbol_size_str, sizeof(symbol_size_str), "%u", symbol_size);
		symbol_size_str[sizeof(symbol_size_str) - 1] = 0;
		stage_context.protocol = "nocode";

		nck_create_encoder(&stages[0], timer, &stage_context, stage_get_opt);
	}

	nck_chain_enc_api(encoder, nck_chain_enc(stages, stage));
	return 0;
}

EXPORT
int nck_chain_create_dec(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	int stage;
	char stage_name[10];
	const char *value;
	uint32_t symbol_size = 1500;
	char symbol_size_str[10];
	struct nck_decoder stages[MAX_STAGES];
	struct stage_context stage_context = { stage_name, context, get_opt, symbol_size_str, NULL };


	value = get_opt(context, "symbol_size");
	if (nck_parse_u32(&symbol_size, value)) {
		fprintf(stderr, "Invalid symbol_size: %s\n", value);
		return -1;
	}

	for (stage = 0; stage < MAX_STAGES; ++stage) {
		sprintf(stage_name, "stage%d", stage);
		value = get_opt(context, stage_name);
		if (!value) {
			break;
		}

		snprintf(symbol_size_str, sizeof(symbol_size_str), "%u", symbol_size);
		symbol_size_str[sizeof(symbol_size_str) - 1] = 0;
		stage_context.protocol = value;

		nck_create_decoder(&stages[stage], timer, &stage_context, stage_get_opt);
		// propagate the size of coded packets
		symbol_size = stages[stage].coded_size;
	}

	if (stage == 0) {
		// to make the tests work we create a nocode stage
		stage = 1;
		snprintf(symbol_size_str, sizeof(symbol_size_str), "%u", symbol_size);
		symbol_size_str[sizeof(symbol_size_str) - 1] = 0;
		stage_context.protocol = "nocode";

		nck_create_decoder(&stages[0], timer, &stage_context, stage_get_opt);
	}

	nck_chain_dec_api(decoder, nck_chain_dec(stages, stage));
	return 0;
}
