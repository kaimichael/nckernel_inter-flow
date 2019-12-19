#ifndef _NCK_EXAMPLES_SIMULATOR_COMMON_H_
#define _NCK_EXAMPLES_SIMULATOR_COMMON_H_

#include <nckernel/nckernel.h>
#include <nckernel/api.h>
#include <nckernel/timer.h>
#include <stdbool.h>
#include <stdio.h>
#include "list.h"

struct path {
	const char *name;

	double loss;
	int bps;
	struct timeval latency;
	double max_jitter_ms;

	struct nck_timer *timer;
	int ready;
	struct nck_timer_entry *ready_timer;
};

struct pkt_info {
	struct list_head list;
	struct timeval sent;
	uint32_t seq_no;
	ssize_t len;
	uint32_t hash;
};

struct coder_peer {
	enum nck_coder_type type;
	union {
		struct nck_encoder *enc;
		struct nck_recoder *rec;
		struct nck_decoder *dec;
		void *dummy;
	};
};

struct path_destination {
	struct coder_peer dst;
	double loss;
};

struct path_definition {
	const char *name;
	int bps;
	double loss;
	int latency_ms;
	double max_jitter_ms;
	bool is_feedback;

	struct coder_peer src;
	struct path_destination *dsts;

	struct path path;
};

struct simulator_cfg {
	struct path_definition *path_definitions;
	size_t num_paths;

	struct nck_encoder *enc;
	struct nck_decoder *dec;
	struct nck_recoder **recs;
};

struct simulator_stat {
	struct list_head hash_list;
	struct list_head lost_hash_list;
	uint32_t latency;
	uint32_t sent;
	uint32_t delivered;
    	bool complete;
};

int run_simulator(FILE *jsonf, struct simulator_cfg *cfg);

#endif /* _NCK_EXAMPLES_SIMULATOR_COMMON_H_ */
