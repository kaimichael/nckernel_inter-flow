#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <nckernel/config.h>
#include <nckernel/nckernel.h>
#ifdef ENABLE_NOCODE
  #include <nckernel/nocode.h>
#endif
#ifdef ENABLE_REP
  #include <nckernel/rep.h>
#endif
#ifdef ENABLE_NOACK
  #include <nckernel/noack.h>
#endif
#ifdef ENABLE_GACK
  #include <nckernel/gack.h>
#endif
#ifdef ENABLE_GSAW
  #include <nckernel/gsaw.h>
#endif
#ifdef ENABLE_SLIDING_WINDOW
  #include <nckernel/sw.h>
#endif
#ifdef ENABLE_INTERFLOW_SLIDING_WINDOW
  #include <nckernel/interflowsw.h>
#endif
#ifdef ENABLE_PACE
  #include <nckernel/pace.h>
#endif
#ifdef ENABLE_PACEMG
  #include <nckernel/pacemg.h>
#endif
#ifdef ENABLE_CODARQ
  #include <nckernel/codarq.h>
#endif
#ifdef ENABLE_TETRYS
  #include <nckernel/tetrys.h>
#endif
#ifdef ENABLE_CHAIN
  #include <nckernel/chain.h>
#endif

#include "private.h"
#include "config.h"

static struct {
	const char *name;
	int (*create_encoder)(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
	int (*create_decoder)(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
	int (*create_recoder)(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
	const char *description;
} protocols[] = {
#ifdef ENABLE_NOCODE
	{"nocode", nck_nocode_create_enc, nck_nocode_create_dec, nck_nocode_create_rec,
		"Sends source packets without any modification."},
#endif
#ifdef ENABLE_REP
	{"rep", nck_rep_create_enc, nck_rep_create_dec, nck_rep_create_rec,
		"Implementation of a simple repetition code."},
#endif
#ifdef ENABLE_NOACK
	{"noack", nck_noack_create_enc, nck_noack_create_dec, nck_noack_create_rec,
		"Sends with a fixed redundancy and no feedback."},
#endif
#ifdef ENABLE_GACK
	{"gack", nck_gack_create_enc, nck_gack_create_dec, nck_gack_create_rec,
		"Sends without pause until an acknowledgement is received."},
#endif
#ifdef ENABLE_GSAW
	{"gsaw", nck_gsaw_create_enc, nck_gsaw_create_dec, NULL,
		"Protocol with a generation-based stop&wait ARQ."},
#endif
#ifdef ENABLE_INTERFLOW_SLIDING_WINDOW
	{"interflow_sw", nck_interflow_sw_create_enc, nck_interflow_sw_create_dec, nck_interflow_sw_create_rec,
		"Interflow Sliding window with feedback."},
#endif
#ifdef ENABLE_SLIDING_WINDOW
	{"sliding_window", nck_sw_create_enc, nck_sw_create_dec, nck_sw_create_rec,
		"Sliding window with feedback."},
#endif
#ifdef ENABLE_PACE
	{"pace", nck_pace_create_enc, nck_pace_create_dec, nck_pace_create_rec,
		"Protocol based on noack but with paced redundancy and rare feedbacks. Suited for high RTT applications"},
#endif
#ifdef ENABLE_PACEMG
	{"pacemg", nck_pacemg_create_enc, nck_pacemg_create_dec, nck_pacemg_create_rec,
	 "Protocol based on noack but with paced redundancy, rare feedbacks and multiple generations. Suited for high RTT applications"},
#endif
#ifdef ENABLE_CODARQ
	{"codarq", nck_codarq_create_enc, nck_codarq_create_dec, NULL,
	 "Coded ARQ protocol based on Medard research paper, with multi generations and acknowledgements"},
#endif
#ifdef ENABLE_TETRYS
	{"tetrys", nck_tetrys_create_enc, nck_tetrys_create_dec, NULL,
		"Implementation of Tetrys protocol"},
#endif
#ifdef ENABLE_CHAIN
	{"chain", nck_chain_create_enc, nck_chain_create_dec, NULL,
		"Chain of multiple protocols"},
#endif
	{NULL}
};

EXPORT
const char *nck_version()
{
	return NCKERNEL_VERSION;
}

EXPORT
const char *nck_revision()
{
	return NCKERNEL_REVISION;
}

EXPORT
int nck_protocol_find(const char *name)
{
	if (name == NULL) {
		return -1;
	}

	for (int i = 0; protocols[i].name; ++i) {
		if (!strcmp(protocols[i].name, name)) {
			return i;
		}
	}
	return -1;
}

EXPORT
const char *nck_protocol_name(int index)
{
	return protocols[index].name;
}

EXPORT
const char *nck_protocol_descr(int index)
{
	return protocols[index].description;
}

int nck_parse_u32(uint32_t *value, const char *name)
{
	char dummy;
	uint32_t buffer;
	if (name == NULL || !strcmp(name, "")) {
		return 0;
	} else if (sscanf(name, "%u%c", &buffer, &dummy) == 1) {
		*value = buffer;
		return 0;
	} else {
		return -1;
	}
}

int nck_parse_s32(int32_t *value, const char *name)
{
	char dummy;
	int32_t buffer;
	if (name == NULL || !strcmp(name, "")) {
		return 0;
	} else if (sscanf(name, "%d%c", &buffer, &dummy) == 1) {
		*value = buffer;
		return 0;
	} else {
		return -1;
	}
}

int nck_parse_u16(uint16_t *value, const char *name)
{
	char dummy;
	uint16_t buffer;
	if (name == NULL || !strcmp(name, "")) {
		return 0;
	} else if (sscanf(name, "%hu%c", &buffer, &dummy) == 1) {
		*value = buffer;
		return 0;
	} else {
		return -1;
	}
}

int nck_parse_u8(uint8_t *value, const char *name)
{
	char dummy;
	uint8_t buffer;
	if (name == NULL || !strcmp(name, "")) {
		return 0;
	} else if (sscanf(name, "%hhu%c", &buffer, &dummy) == 1) {
		*value = buffer;
		return 0;
	} else {
		return -1;
	}
}

int nck_parse_timeval(struct timeval *value, const char *string)
{
	double number;
	double sec;
	char *end;

	if (string == NULL || !strcmp(string, "")) {
		return 0;
	}

	number = strtod(string, &end);
	while (isspace(*end)) {
		++end;
	}

	if (*end == 0 || !strcmp("s", end) || !strcmp("sec", end)) {
		sec = number;
	} else if (!strcmp("ms", end) || !strcmp("msec", end)) {
		sec = number / 1000.0;
	} else if (!strcmp("us", end) || !strcmp("usec", end)) {
		sec = number / 1000000.0;
	} else if (!strcmp("ns", end) || !strcmp("nsec", end)) {
		sec = number / 1000000000.0;
	} else {
		fprintf(stderr, "Invalid unit for time value: %s\n", string);
		return -1;
	}

	value->tv_sec = sec;
	value->tv_usec = (sec - floor(sec)) * 1000000.0;
	return 0;
}

static const char *stub_get_opt(void *context, const char *option)
{
	// remove unused parameter warning
	((void)context);
	((void)option);

	return NULL;
}

EXPORT
int nck_create_coder(struct nck_coder *coder, enum nck_coder_type type, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	switch (type) {
		case NCK_ENCODER:
			return nck_create_encoder((struct nck_encoder*)coder, timer, context, get_opt);

		case NCK_DECODER:
			return nck_create_decoder((struct nck_decoder*)coder, timer, context, get_opt);

		case NCK_RECODER:
			return nck_create_recoder((struct nck_recoder*)coder, timer, context, get_opt);

		default:
			return -1;
	}
}

EXPORT
int nck_create_encoder(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *proto;
	int index;

	if (!get_opt)
		get_opt = stub_get_opt;

	proto = get_opt(context, "protocol");
	if (proto == NULL) {
		assert(protocols[0].create_encoder != NULL);
		return protocols[0].create_encoder(encoder, timer, context, get_opt);
	}

	index = nck_protocol_find(proto);
	if (index >= 0) {
		assert(protocols[index].create_encoder != NULL);
		return protocols[index].create_encoder(encoder, timer, context, get_opt);
	}

	fprintf(stderr, "Unknown protocol: %s\n", proto);
	return -1;
}

EXPORT
int nck_create_decoder(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *proto;
	int index;

	if (!get_opt)
		get_opt = stub_get_opt;

	proto = get_opt(context, "protocol");
	if (proto == NULL) {
		assert(protocols[0].create_decoder != NULL);
		return protocols[0].create_decoder(decoder, timer, context, get_opt);
	}

	index = nck_protocol_find(proto);
	if (index >= 0) {
		assert(protocols[index].create_decoder != NULL);
		return protocols[index].create_decoder(decoder, timer, context, get_opt);
	}

	fprintf(stderr, "Unknown protocol: %s\n", proto);
	return -1;
}

EXPORT
int nck_create_recoder(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt)
{
	const char *proto;
	int index;

	if (!get_opt)
		get_opt = stub_get_opt;

	proto = get_opt(context, "protocol");
	if (proto == NULL) {
		assert(protocols[0].create_recoder != NULL);
		return protocols[0].create_recoder(recoder, timer, context, get_opt);
	}

	index = nck_protocol_find(proto);
	if (index >= 0) {
		assert(protocols[index].create_recoder != NULL);
		return protocols[index].create_recoder(recoder, timer, context, get_opt);
	}

	fprintf(stderr, "Unknown protocol: %s\n", proto);
	return -1;
}

const char *nck_option_from_array(void *context, const char *name)
{
	struct nck_option_value *opt;

	for (opt = (struct nck_option_value *)context; opt->name; ++opt) {
		if (!strcmp(opt->name, name)) {
			return opt->value;
		}
	}
	return NULL;
}
