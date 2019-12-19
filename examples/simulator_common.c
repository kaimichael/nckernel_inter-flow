#include "simulator_common.h"
#include "../src/private.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <assert.h>

#include <nckernel/nckernel.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

static FILE *json = NULL;

static void path_rearm(struct nck_timer_entry *entry, void *path_ptr, int success)
{
	(void)entry; // unused parameter

	struct path *path = (struct path *)path_ptr;
	if (success) {
		path->ready = 1;
	}
}

static void path_init(struct path *path, const char *name,
		      struct nck_timer *timer,
		      int bps, int latency_ms, double max_jitter_ms)
{
	path->name = name;
	path->bps = bps;
	path->ready = 1;
	path->timer = timer;
	path->max_jitter_ms = max_jitter_ms;

	timerclear(&path->latency);
	path->latency.tv_sec = latency_ms / 1000;
	path->latency.tv_usec = (latency_ms % 1000) * 1000;

	path->ready_timer = nck_timer_add(path->timer, NULL, (void *)path, &path_rearm);
}

static void path_free(struct path *path)
{
	nck_timer_cancel(path->ready_timer);
	nck_timer_free(path->ready_timer);
}

struct frame
{
	struct coder_peer *src;
	struct path_destination *dsts;

	struct nck_schedule *schedule;
	struct path *path;
	struct timeval send;
	struct timeval receive;
	int success;

	const char *pkt_desc;
	const char *coder_desc;

	struct sk_buff packet;

	uint8_t buffer[];
};

struct frame_clone {
	struct sk_buff packet;
	uint8_t buffer[];
};

static struct frame_clone * clone_frame(struct frame *frame)
{
	struct frame_clone *result;
	size_t size;

	size = frame->packet.end - frame->packet.head;

	result = malloc(sizeof(*result) + size);
	if (!result)
		return NULL;

	memcpy(result->buffer, frame->buffer, size);
	skb_new_clone(&result->packet, result->buffer, &frame->packet);

	return result;
}

static void frame_to_json(struct frame *frame)
{
	static int first = 1;

	if (json == NULL) {
		return;
	}

	if (!first) {
		fprintf(json, ",\n");
	}

	first = 0;

	fprintf(json, "{"
		"\"path\":\"%s\", "
		"\"send\":%ld.%06ld, "
		"\"receive\":%ld.%06ld, "
		"\"success\":%d, "
		"\"length\":%u",
		frame->path->name,
		frame->send.tv_sec, frame->send.tv_usec,
		frame->receive.tv_sec, frame->receive.tv_usec,
		frame->success, frame->packet.len);

	if (frame->pkt_desc && frame->pkt_desc[0]) {
		fprintf(json, ", %s", frame->pkt_desc);
	}

	if (frame->coder_desc && frame->coder_desc[0]) {
		fprintf(json, ", \"coder\":{%s}", frame->coder_desc);
	}

	fprintf(json, "}");
}

/**
 * Simple non-cryptographic hash function based on Jenkins's one-at-a-time hash
 * @param key - Start of the buffer
 * @param length - Length of the buffer
 * @return
 */
uint32_t hash(const uint8_t *key, size_t length) {
	size_t i = 0;
	uint32_t hash = 0;
	while (i != length) {
		hash += key[i++];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;
	return hash;
}

static double get_dst_default_loss(struct frame *frame)
{
	size_t i;

	for (i = 0; frame->dsts && frame->dsts[i].dst.dummy; i++) {
		struct path_destination *dst = &frame->dsts[i];

		if (dst->dst.type == NCK_ENCODER ||
		    dst->dst.type == NCK_DECODER)
			return dst->loss;
	}

	/* there is no encoder/decoder destination and thus the loss is 100% */
	return 1.0;
}

static void path_send(struct path *path, int len, struct frame *frame, nck_timer_callback callback)
{
	struct timeval delay,delay_after_jitter;
	uint64_t bits = (uint64_t)len * 8;
	delay.tv_sec = bits / path->bps;
	delay.tv_usec = bits * 1000000 / path->bps;

	double jitter = path->max_jitter_ms*((drand48()*2 - 1));
	if(jitter == 0){
		delay_after_jitter = path->latency;
	}else{
		delay_after_jitter.tv_sec = path->latency.tv_sec + ((int)jitter/1000);
		delay_after_jitter.tv_usec = (int)(path->latency.tv_usec + ((jitter - (double) delay_after_jitter.tv_sec * 1000) * 1000));
	}

	frame->success = drand48() >= get_dst_default_loss(frame);
	timeradd(&frame->schedule->time, &delay_after_jitter, &frame->receive);
	timeradd(&frame->receive, &delay, &frame->receive);
	frame_to_json(frame);

	nck_timer_add(path->timer, &delay_after_jitter, frame, callback);

	path->ready = 0;
	nck_timer_rearm(path->ready_timer, &delay);
}

static void receive_data(struct nck_timer_entry *handle, void *context, int success)
{
	struct frame *frame = (struct frame *)context;
	size_t i;
	struct frame_clone *cframe;
	int peer_success;

	if (success) {
		for (i = 0; frame->dsts && frame->dsts[i].dst.dummy; i++) {
			struct path_destination *dst = &frame->dsts[i];

			if (dst->dst.type == NCK_RECODER) {
				/* calculate the per packet success for recoder */
				peer_success = drand48() >= dst->loss;
			} else {
				/* the decoder success was pre-calculated for the json output */
				peer_success = frame->success;
			}

			if (!peer_success)
				continue;

			cframe = clone_frame(frame);
			if (!cframe)
				continue;

			switch (dst->dst.type) {
			case NCK_ENCODER:
				/* nck_put_coded(dst->dst.enc, &cframe->packet); */
				assert(0);
				break;
			case NCK_DECODER:
				nck_put_coded(dst->dst.dec, &cframe->packet);
				break;
			case NCK_RECODER:
				nck_put_coded(dst->dst.rec, &cframe->packet);
				break;
			}

			free(cframe);
		}
	}

	if (handle) {
		nck_timer_free(handle);
	}

	free(frame);
}

#define send_data(_path, _frame, _coder) __extension__ ({ \
	struct path *__path = (_path); \
	struct frame *__frame = (_frame); \
	typeof(_coder) __coder = (_coder); \
	\
	nck_get_coded(__coder, &__frame->packet); \
	\
	__frame->pkt_desc = nck_describe_packet(__coder, &__frame->packet); \
	__frame->coder_desc = nck_debug(__coder); \
	\
	path_send(__path, __frame->packet.len, __frame, &receive_data); \
	/* fprintf(stderr, "send data "); */ \
	/* skb_print_part(stderr, &__frame->packet, 32, 8, 5); */ \
})

static void receive_feedback(struct nck_timer_entry *handle, void *context, int success)
{
	struct frame *frame = (struct frame *)context;
	size_t i;
	struct frame_clone *cframe;
	int peer_success;

	if (success) {
		for (i = 0; frame->dsts && frame->dsts[i].dst.dummy; i++) {
			struct path_destination *dst = &frame->dsts[i];

			if (dst->dst.type == NCK_RECODER) {
				/* calculate the per packet success for recoder */
				peer_success = drand48() >= dst->loss;
			} else {
				/* the encoder success was pre-calculated for the json output */
				peer_success = frame->success;
			}

			if (!peer_success)
				continue;

			cframe = clone_frame(frame);
			if (!cframe)
				continue;

			switch (dst->dst.type) {
			case NCK_ENCODER:
				nck_put_feedback(dst->dst.enc, &cframe->packet);
				break;
			case NCK_DECODER:
				/* nck_put_feedback(dst->dst.dec, &cframe->packet); */
				assert(0);
				break;
			case NCK_RECODER:
				nck_put_feedback(dst->dst.rec, &cframe->packet);
				break;
			}

			free(cframe);
		}
	}

	if (handle) {
		nck_timer_free(handle);
	}

	free(frame);
}

#define send_feedback(_path, _frame, _coder) __extension__ ({ \
	struct path *__path = (_path); \
	struct frame *__frame = (_frame); \
	typeof(_coder) __coder = (_coder); \
	\
	nck_get_feedback(__coder, &__frame->packet); \
	\
	__frame->pkt_desc = nck_describe_packet(__coder, &__frame->packet); \
	__frame->coder_desc = nck_debug(__coder); \
	\
	path_send(__path, __frame->packet.len, __frame, &receive_feedback);\
})

static struct frame * new_frame(struct nck_schedule *sched, struct path *path,
				struct coder_peer *src, struct path_destination *dsts,
				size_t size)
{
	struct frame *result;

	result = malloc(sizeof(*result) + size);
	result->path = path;
	result->schedule = sched;
	result->send = sched->time;
	timerclear(&result->receive);
	result->success = 0;
	result->src = src;
	result->dsts = dsts;
	result->pkt_desc = NULL;
	result->coder_desc = NULL;

	skb_new(&result->packet, result->buffer, size);

	return result;
}

static int read_input(struct nck_schedule *sched, struct nck_encoder *enc, int reader, size_t source_size, struct simulator_stat *stat)
{
	struct sk_buff packet;
	uint8_t buffer[source_size];
	ssize_t len;
	struct pkt_info *pkt_info;

	while (!nck_full(enc)) {
		memset(buffer, 0, sizeof(buffer));

		skb_new(&packet, buffer, sizeof(buffer));
		skb_reserve(&packet, sizeof(uint16_t));

		len = read(reader, packet.data, skb_tailroom(&packet));
		if (len < 0) {
			fprintf(stderr, "Failed to read from source\n");
			exit(1);
		}
		skb_put(&packet, len);

		pkt_info = malloc(sizeof(struct pkt_info));
		pkt_info->hash = hash(packet.data, len);
		pkt_info->seq_no = stat->sent;
		pkt_info->len = len;
		pkt_info->sent = sched->time;
		list_add_tail(&pkt_info->list, &stat->hash_list);
		stat->sent += 1;

		skb_push_u16(&packet, len);

		nck_put_source(enc, &packet);

		if (len == 0) {
			return 1;
		}
	}

	return 0;
}

static int write_output(struct nck_schedule *sched, struct nck_decoder *dec, int writer, size_t source_size, struct simulator_stat *stat)
{
	struct sk_buff packet;
	uint8_t buffer[source_size];
	ssize_t ret;
	size_t len;
	int success;
	uint32_t hash_rx;
	struct pkt_info *hash_sent, *hash_safe;

	while (nck_has_source(dec)) {
		skb_new(&packet, buffer, sizeof(buffer));
		if (nck_get_source(dec, &packet)) {
			fprintf(stderr, "Error decoding data\n");
			exit(1);
		}

		len = skb_pull_u16(&packet);
		assert(len <= source_size);

		hash_rx = hash(packet.data, len);
		list_for_each_entry_safe(hash_sent, hash_safe, &stat->hash_list, list) {
			success = hash_sent->hash == hash_rx;

			if (json != NULL) {
				fprintf(json, ",\n{"
						"\"path\":\"source\", "
						"\"send\":%ld.%06ld, "
						"\"receive\":%ld.%06ld, "
						"\"success\":%d, "
						"\"length\":%zd }",
						hash_sent->sent.tv_sec, hash_sent->sent.tv_usec,
						sched->time.tv_sec, sched->time.tv_usec,
						success,
						hash_sent->len
					   );
			}

			if (success) {
				stat->delivered +=1;
				list_del(&hash_sent->list);
				free(hash_sent);
				break;
			} else {
				list_del(&hash_sent->list);
				list_add_tail(&hash_sent->list, &stat->lost_hash_list);
			}

		}

		if (len == 0) {
			// if we received an empty symbol we are done
			return 1;
		}

		ret = write(writer, packet.data, len);
		if (ret < 0) {
			perror("write");
			exit(1);
		}
	}

	return 0;
}

static const char *get_env_opt(void *context, const char *option)
{
	char c;
	const char *result;
	size_t pos = 0;
	char envname[256];

	// remove unused parameter warning
	((void)context);

	while (option[pos]) {
		c = option[pos];
		if (isalpha(c)) {
			envname[pos++] = toupper(c);
		} else if (isdigit(c)) {
			envname[pos++] = c;
		} else {
			envname[pos++] = '_';
		}
	}

	envname[pos] = '\0';
	result = getenv(envname);
	fprintf(stderr, "%s -> %s = %s\n", option, envname, result);

	return result;
}

static bool path_has_coded(struct path_definition *path_def)
{
	assert(!path_def->is_feedback);

	switch (path_def->src.type) {
	case NCK_ENCODER:
		return nck_has_coded(path_def->src.enc);
	case NCK_DECODER:
		/* return nck_has_coded(path_def->src.dec); */
		return false;
	case NCK_RECODER:
		return nck_has_coded(path_def->src.rec);
	}

	return false;
}

static bool path_has_feedback(struct path_definition *path_def)
{
	assert(path_def->is_feedback);

	switch (path_def->src.type) {
	case NCK_ENCODER:
		/* return nck_has_feedback(path_def->src.enc); */
		return false;
	case NCK_DECODER:
		return nck_has_feedback(path_def->src.dec);
	case NCK_RECODER:
		return nck_has_feedback(path_def->src.rec);
	}

	return false;
}

static void path_send_coded(struct path_definition *path_def, struct frame *frame)
{
	assert(!path_def->is_feedback);

	switch (path_def->src.type) {
	case NCK_ENCODER:
		send_data(&path_def->path, frame, path_def->src.enc);
		break;
	case NCK_DECODER:
		/* send_data(&path_def->path, frame, path_def->src.dec); */
		assert(0);
		break;
	case NCK_RECODER:
		send_data(&path_def->path, frame, path_def->src.rec);
		break;
	}
}

static void path_send_feedback(struct path_definition *path_def, struct frame *frame)
{
	assert(path_def->is_feedback);

	switch (path_def->src.type) {
	case NCK_ENCODER:
		/* send_feedback(&path_def->path, frame, path_def->src.enc); */
		assert(0);
		break;
	case NCK_DECODER:
		send_feedback(&path_def->path, frame, path_def->src.dec);
		break;
	case NCK_RECODER:
		send_feedback(&path_def->path, frame, path_def->src.rec);
		break;
	}
}

static bool has_pending_packets(struct simulator_cfg *cfg)
{
	size_t i;
	struct path_definition *path_def;

	/* check for possible coded packet transfer */
	for (i = 0; i < cfg->num_paths; i++) {
		path_def = &cfg->path_definitions[i];
		if (path_def->is_feedback)
			continue;

		/* path is not ready because it is transfering packets */
		if (path_def->path.ready)
			return true;

		/* it has coded packets waiting which can be transferred */
		if (path_has_coded(path_def))
			return true;
	}

	/* check for possible feedback packet transfer */
	for (i = 0; i < cfg->num_paths; i++) {
		path_def = &cfg->path_definitions[i];
		if (!path_def->is_feedback)
			continue;

		/* path is not ready because it is transfering packets */
		if (path_def->path.ready)
			return true;

		/* it has coded packets waiting which can be transferred */
		if (path_has_feedback(path_def))
			return true;
	}

	return false;
}

static void free_stat(struct simulator_stat *stat) {
	struct pkt_info *p_info_tmp, *p_info_safe;
	list_for_each_entry_safe(p_info_tmp, p_info_safe, &stat->lost_hash_list, list) {
		list_del(&p_info_tmp->list);
		free(p_info_tmp);
	}
	list_for_each_entry_safe(p_info_tmp, p_info_safe, &stat->hash_list, list) {
		list_del(&p_info_tmp->list);
		free(p_info_tmp);
	}

}

static void print_lost_stat(struct simulator_stat *stat) {
	// NOTE: This list may not display the last few packets lost in the simulation.
	// Because we do not know a pkt is lost until we recv another pkt with a higher seq nr.
	struct pkt_info *p_info_tmp;
	fprintf(stderr, "\nLost Packets:\nSeq.No\tLen\t\tHash\n");
	list_for_each_entry(p_info_tmp, &stat->lost_hash_list, list) {
		fprintf(stderr, "%*d\t%*zu\t%010u\t \n",
			6, p_info_tmp->seq_no, 4, p_info_tmp->len, p_info_tmp->hash);
	}
}

static void timeout_simulation(struct nck_timer_entry *handle, void *context, int success) {
	UNUSED(handle);
	if (success) {
		struct simulator_stat *stat = (struct simulator_stat *) context;
		stat->complete = true;
	}
}

static void print_stats(struct simulator_cfg *cfg)
{
	unsigned i, j;
	unsigned max_stat_len = 0, l;
	char keyfmt[20], name[20];

	for (i = 0; i < NCK_STATS_MAX; ++i) {
		l = strlen(nck_stat_string(i));
		if (l > max_stat_len) {
			max_stat_len = l;
		}
	}

	// title row
	sprintf(keyfmt, "%%%us", max_stat_len);
	fprintf(stderr, keyfmt, "stat");

	if (nck_get_stats(cfg->enc) != NULL) {
		fprintf(stderr, " %8s", "enc");
	}

	for (j = 0; cfg->recs && cfg->recs[j]; j++) {
		if (nck_get_stats(cfg->recs[j]) != NULL) {
			sprintf(name, "rec%u", j+1);
			fprintf(stderr, " %8s", name);
		}
	}
	if (nck_get_stats(cfg->dec) != NULL) {
		fprintf(stderr, " %8s", "dec");
	}
	fprintf(stderr, "\n");

	for (i = 0; i < NCK_STATS_MAX; ++i) {
		fprintf(stderr, keyfmt, nck_stat_string(i));
		if (nck_get_stats(cfg->enc) != NULL) {
			fprintf(stderr, " %8lu", nck_get_stats(cfg->enc)->s[i]);
		}
		for (j = 0; cfg->recs && cfg->recs[j]; j++) {
			if (nck_get_stats(cfg->recs[j]) != NULL) {
				fprintf(stderr, " %8lu", nck_get_stats(cfg->recs[j])->s[i]);
			}
		}
		if (nck_get_stats(cfg->enc) != NULL) {
			fprintf(stderr, " %8lu", nck_get_stats(cfg->dec)->s[i]);
		}
		fprintf(stderr, "\n");
	}
}

int run_simulator(FILE *jsonf, struct simulator_cfg *cfg)
{
	struct nck_recoder *rec;
	struct nck_schedule sched;
	struct nck_timer timer;
	int reader, writer;
	int eof = 0;
	struct frame *frame;
	struct timeval step;
	size_t source_size, coded_size, feedback_size;
	struct path_definition *path_def;
	size_t i;
	struct timeval sim_complete_timeout = {.tv_sec=5, .tv_usec=0};

	struct simulator_stat stat = {0};
	INIT_LIST_HEAD(&stat.hash_list);
	INIT_LIST_HEAD(&stat.lost_hash_list);

	json = jsonf;

	nck_schedule_init(&sched);
	nck_schedule_timer(&sched, &timer);
	struct nck_timer_entry *sim_complete_handle = nck_timer_add(&timer, NULL, &stat, &timeout_simulation);

	if (nck_create_decoder(cfg->dec, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create decoder");
		return -1;
	}
	assert(!nck_has_source(cfg->dec));
	assert(!nck_has_feedback(cfg->dec));

	if (nck_create_encoder(cfg->enc, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create encoder");
		return -1;
	}
	assert(!nck_has_coded(cfg->enc));
	assert(!nck_full(cfg->enc));

	for (i = 0; cfg->recs && cfg->recs[i]; i++) {
		rec = cfg->recs[i];

		if (nck_create_recoder(rec, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create recoder");
		return -1;
		}
		assert(!nck_has_coded(rec));
		assert(!nck_has_feedback(rec));
	}

	source_size = cfg->enc->source_size;
	feedback_size = cfg->enc->feedback_size;
	coded_size = cfg->enc->coded_size;

	for (i = 0; i < cfg->num_paths; i++) {
		path_def = &cfg->path_definitions[i];

		path_init(&path_def->path, path_def->name, &timer,
			  path_def->bps, path_def->latency_ms, path_def->max_jitter_ms);
	}

	reader = fileno(stdin);
	writer = fileno(stdout);
	while ((!write_output(&sched, cfg->dec, writer, source_size, &stat)) && (!stat.complete)) {
		// run all scheduled events
		nck_schedule_run(&sched, &step);

		if (!eof) {
			eof = read_input(&sched, cfg->enc, reader, source_size, &stat);
			if(eof) {
				//Start timeout to finish simulation
				nck_timer_rearm(sim_complete_handle, &sim_complete_timeout);
			}
		}

		/* check for possible coded packet transfer */
		for (i = 0; i < cfg->num_paths; i++) {
			path_def = &cfg->path_definitions[i];
			if (path_def->is_feedback)
				continue;

			if (!path_def->path.ready)
				continue;

			if (!path_has_coded(path_def))
				continue;

			frame = new_frame(&sched, &path_def->path, &path_def->src, path_def->dsts, coded_size);
			path_send_coded(path_def, frame);
		}

		/* check for possible feedback packet transfer */
		for (i = 0; i < cfg->num_paths; i++) {
			path_def = &cfg->path_definitions[i];
			if (!path_def->is_feedback)
				continue;

			if (!path_def->path.ready)
				continue;

			if (!path_has_feedback(path_def))
				continue;

			frame = new_frame(&sched, &path_def->path, &path_def->src, path_def->dsts, feedback_size);
			path_send_feedback(path_def, frame);
		}

		// update the step because it could have changed
		if (nck_schedule_run(&sched, &step)) {
			// try to flush
			if (eof && !has_pending_packets(cfg)) {
				nck_flush_coded(cfg->enc);
				if (nck_schedule_run(&sched, &step)) {
					break;
				}
			}
		}

		timeradd(&sched.time, &step, &sched.time);
	}

	print_stats(cfg);

	nck_free(cfg->enc);
	nck_free(cfg->dec);

	for (i = 0; cfg->recs && cfg->recs[i]; i++) {
		rec = cfg->recs[i];

		nck_free(rec);
	}

	for (i = 0; i < cfg->num_paths; i++) {
		path_def = &cfg->path_definitions[i];

		path_free(&path_def->path);
	}

	nck_schedule_free_all(&sched);
	nck_timer_free(sim_complete_handle);

	if (json)
		fprintf(json, "]}");

	fprintf(stderr, "\n**** Simulation Results ****"
			"\nPackets Sent      = %d"
			"\nPackets Delivered = %d"
			"\nLoss ratio        = %.2f%%\n\n",
		stat.sent, stat.delivered, ((float) 100 * (stat.sent - stat.delivered))/stat.sent);

	fprintf(stderr, "Time to transfer the file: %ld.%06ldseconds\n",
		sched.time.tv_sec, sched.time.tv_usec);

	if (stat.sent - stat.delivered > 0) {
		print_lost_stat(&stat);
	}
	free_stat(&stat);

	return 0;
}

