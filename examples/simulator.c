#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simulator_common.h"
#include "../src/private.h"


static struct nck_encoder enc;
struct nck_decoder dec;
struct nck_recoder rec1;
struct nck_recoder rec2;

#define PATH_DST_DECODER(_loss) { .dst.type = NCK_DECODER, .dst.dec = &dec, .loss = (_loss) }
#define PATH_DST_ENCODER(_loss) { .dst.type = NCK_ENCODER, .dst.enc = &enc, .loss = (_loss) }
#define PATH_DST_RECODER(_rec, _loss) { .dst.type = NCK_RECODER, .dst.rec = &(_rec), .loss = (_loss) }
#define PATH_DST_END { .dst.type = 0, .dst.dummy = NULL, 0 }

#define PATH_SRC_DECODER .src.type = NCK_DECODER, .src.dec = &dec
#define PATH_SRC_ENCODER .src.type = NCK_ENCODER, .src.enc = &enc
#define PATH_SRC_RECODER(_rec) .src.type = NCK_RECODER, .src.rec = &_rec

/* Default configuration with one encoder and one decoder (no loss)
 *
 * - the data forwarding path is lossless
 * - Feedback path is lossless
 */

static struct path_destination cfg_default_noloss_dst_data_enc[] = {
	PATH_DST_DECODER(0.0),
	PATH_DST_END
};

static struct path_destination cfg_default_noloss_dst_feedback_dec[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_END
};

static struct path_definition cfg_default_noloss_paths[] = {
		{
			.name = "data",
			.bps = 8000000,
			.loss =  0.1,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_ENCODER,
			.dsts =  cfg_default_noloss_dst_data_enc,
		},
		{
			.name = "feedback",
			.bps = 8000000,
			.loss = 0,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_DECODER,
			.dsts = cfg_default_noloss_dst_feedback_dec,
		},
};

static struct simulator_cfg cfg_default_noloss = {
	.enc = &enc,
	.dec = &dec,

	.path_definitions = cfg_default_noloss_paths,
	.num_paths = ARRAY_SIZE(cfg_default_noloss_paths),
};

/* Default configuration with one encoder and one decoder
 *
 * - the data forwarding path has loss
 * - Feedback path is lossless
 */

static struct path_destination cfg_default_dst_data_enc[] = {
	PATH_DST_DECODER(0.1),
	PATH_DST_END
};

static struct path_destination cfg_default_dst_feedback_dec[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_END
};

static struct path_definition cfg_default_paths[] = {
		{
			.name = "data",
			.bps = 8000000,
			.loss =  0.1,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_ENCODER,
			.dsts =  cfg_default_dst_data_enc,
		},
		{
			.name = "feedback",
			.bps = 8000000,
			.loss = 0,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_DECODER,
			.dsts = cfg_default_dst_feedback_dec,
		},
};

static struct simulator_cfg cfg_default = {
	.enc = &enc,
	.dec = &dec,

	.path_definitions = cfg_default_paths,
	.num_paths = ARRAY_SIZE(cfg_default_paths),
};

/* Configuration with single recoder
 *
 * - the data forwarding path has loss and there is a path between:
 *   - encoder and recoder
 *   - recoder and decoder
 * - Feedback path is lossless and there is a path between
 *   - decoder and recoder
 *   - decoder and encoder
 */
static struct path_destination cfg_single_rec_dst_data_enc[] = {
	PATH_DST_RECODER(rec1, 0.1),
	PATH_DST_END
};

static struct path_destination cfg_single_rec_dst_data_rec1[] = {
	PATH_DST_DECODER(0.1),
	PATH_DST_END
};

static struct path_destination cfg_single_rec_dst_feedback_dec[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_RECODER(rec1, 0.0),
	PATH_DST_END
};

static struct path_destination cfg_single_rec_dst_feedback_rec1[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_END
};

static struct nck_recoder *cfg_single_rec_recoder[] = {
	&rec1,
	NULL
};

static struct path_definition cfg_single_rec_paths[] = {
		{
			.name = "data_enc_rec1",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_ENCODER,
			.dsts = cfg_single_rec_dst_data_enc,
		},
		{
			.name = "data_rec1_dec",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_RECODER(rec1),
			.dsts = cfg_single_rec_dst_data_rec1,
		},
		{
			.name = "feedback_dec",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_DECODER,
			.dsts = cfg_single_rec_dst_feedback_dec,
		},
		{
			.name = "feedback_rec1_enc",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_RECODER(rec1),
			.dsts = cfg_single_rec_dst_feedback_rec1,
		},
};

static struct simulator_cfg cfg_single_rec = {
	.enc = &enc,
	.dec = &dec,
	.recs = cfg_single_rec_recoder,

	.path_definitions = cfg_single_rec_paths,
	.num_paths = ARRAY_SIZE(cfg_single_rec_paths),
};

/* Configuration with two recoder
 *
 * - the data forwarding path has loss and there is a path between:
 *   - encoder and recoder1
 *   - recoder1 and recoder2
 *   - recoder2 and decoder
 *   - (overhearing) encoder and recoder2
 *   - (overhearing) recoder1 and decoder
 * - Feedback path is lossless and there is a path between
 *   - decoder and recoder2
 *   - decoder and recoder1
 *   - decoder and encoder
 *   - recoder2 and recoder1
 *   - recoder2 and decoder
 *   - recoder1 and decoder
 */
static struct path_destination cfg_double_rec_dst_data_enc[] = {
	PATH_DST_RECODER(rec1, 0.1),
	PATH_DST_RECODER(rec2, 0.7),
	PATH_DST_END
};

static struct path_destination cfg_double_rec_dst_data_rec1[] = {
	PATH_DST_RECODER(rec2, 0.1),
	PATH_DST_DECODER(0.7),
	PATH_DST_END
};

static struct path_destination cfg_double_rec_dst_data_rec2[] = {
	PATH_DST_DECODER(0.1),
	PATH_DST_END
};

static struct path_destination cfg_double_rec_dst_feedback_dec[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_RECODER(rec1, 0.0),
	PATH_DST_RECODER(rec2, 0.0),
	PATH_DST_END
};

static struct path_destination cfg_double_rec_dst_feedback_rec2[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_RECODER(rec1, 0.0),
	PATH_DST_END
};

static struct path_destination cfg_double_rec_dst_feedback_rec1[] = {
	PATH_DST_ENCODER(0.0),
	PATH_DST_END
};

static struct nck_recoder *cfg_double_rec_recoder[] = {
	&rec1,
	&rec2,
	NULL
};

static struct path_definition cfg_double_rec_paths[] = {
		{
			.name = "data_enc_rec1",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_ENCODER,
			.dsts = cfg_double_rec_dst_data_enc,
		},
		{
			.name = "data_rec1_rec2",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_RECODER(rec1),
			.dsts = cfg_double_rec_dst_data_rec1,
		},
		{
			.name = "data_rec2_dec",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = false,

			PATH_SRC_RECODER(rec2),
			.dsts = cfg_double_rec_dst_data_rec2,
		},
		{
			.name = "feedback_dec",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_DECODER,
			.dsts = cfg_double_rec_dst_feedback_dec,
		},
		{
			.name = "feedback_rec2",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_RECODER(rec1),
			.dsts = cfg_double_rec_dst_feedback_rec2,
		},
		{
			.name = "feedback_rec1",
			.bps = 8000000,
			.latency_ms = 5,
			.max_jitter_ms = 0,
			.is_feedback = true,

			PATH_SRC_RECODER(rec1),
			.dsts = cfg_double_rec_dst_feedback_rec1,
		},
};

static struct simulator_cfg cfg_double_rec = {
	.enc = &enc,
	.dec = &dec,
	.recs = cfg_double_rec_recoder,

	.path_definitions = cfg_double_rec_paths,
	.num_paths = ARRAY_SIZE(cfg_double_rec_paths),
};

/* Registration of topology configurations */
struct name2cfg {
	const char *name;
	struct simulator_cfg *cfg;
};

struct name2cfg name2cfgs[] = {
	{ "default_noloss", &cfg_default_noloss },
	{ "default", &cfg_default },
	{ "single_rec", &cfg_single_rec },
	{ "double_rec", &cfg_double_rec },
};

struct simulator_cfg *find_cfg(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(name2cfgs); i++) {
		if (strcasecmp(name, name2cfgs[i].name) == 0)
			return name2cfgs[i].cfg;
	}

	return NULL;
}

static void validate_cfg(struct simulator_cfg *cfg) {
	for (size_t i = 0; i < cfg->num_paths; i++) {
		if (cfg->path_definitions[i].max_jitter_ms > cfg->path_definitions[i].latency_ms) {
			fprintf(stderr, "Invalid Config: Jitter must be less than the latency\n");
			exit(1);
		}
	}
}

static void usage(int argc, char *argv[])
{
	size_t i;
	const char *prog = "simulator";

	if (argc > 0)
		prog = argv[0];

	fprintf(stderr, "USAGE: %s TOPOLOGY [JSONFILE]\n\n", prog);
	fprintf(stderr, "available topologies:\n");

	for (i = 0; i < ARRAY_SIZE(name2cfgs); i++)
		fprintf(stderr, " * %s\n", name2cfgs[i].name);
}

void seed_rand()
{
	const char *seedstr;
	long seedval = 0;

	seedstr = getenv("SEED");
	if (seedstr) {
		seedval = atol(seedstr);
	}

	srand48(seedval);
	srand(seedval);
}

int main(int argc, char *argv[])
{
	FILE *json = NULL;
	struct simulator_cfg *cfg;
	int ret;

	if (argc < 2) {
		usage(argc, argv);
		exit(1);
	}

	cfg = find_cfg(argv[1]);
	if (!cfg) {
		fprintf(stderr, "Unknown topology: %s\n", argv[1]);
		usage(argc, argv);
		exit(1);
	}

	if (argc > 2) {
		json = fopen(argv[2], "w");
		fprintf(json, "{\"frames\":[\n");
	}

	seed_rand();

	validate_cfg(cfg);
	ret = run_simulator(json, cfg);

	if (json)
		fclose(json);

	return ret;
}
