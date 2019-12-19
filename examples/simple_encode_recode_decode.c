#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>

#include <nckernel/nckernel.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>
#include <math.h>

#include "../src/private.h"


static const char *get_env_opt(void *context, const char *option) {
	char c;
	const char *result;
	size_t pos = 0;
	char envname[256];

	// remove unused parameter warning
	((void) context);

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

	return getenv(envname);
}

void shuffle(uint8_t *array, size_t n) {
	if (n > 1) {
		size_t i;
		for (i = 0; i < n - 1; i++) {
			size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
			uint8_t t = array[j];
			array[j] = array[i];
			array[i] = t;
		}
	}
}

int main() {
	struct nck_encoder enc;
	struct nck_schedule schedule;
	struct timeval step;
	struct nck_timer timer;
	struct timespec clock;
	struct nck_recoder rec, rec2;
	srand(time(NULL));
	struct sk_buff packet, packet2,packet3, packet4, packet5;
	uint32_t seq_num = 0;
	uint16_t seq_gen_num = 0;
	int num_pkts = 32;

	nck_schedule_init(&schedule);
	clock_gettime(CLOCK_MONOTONIC, &clock);
	schedule.time.tv_sec = clock.tv_sec;
	schedule.time.tv_usec = clock.tv_nsec / 1000;
	nck_schedule_timer(&schedule, &timer);

	if (nck_create_encoder(&enc, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create encoder");
		return -1;
	}


	if (nck_create_recoder(&rec, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create recoder");
		return -1;
	}

	if (nck_create_recoder(&rec2, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create recoder");
		return -1;
	}

	uint8_t buffer[enc.source_size];
	memset(buffer, 0, enc.source_size);
	uint8_t buffer2[enc.coded_size];
	memset(buffer2, 0, enc.coded_size);
	uint8_t buffer3[rec.coded_size];
	memset(buffer3, 0, rec.coded_size);
	uint8_t buffer4[rec.source_size];
	memset(buffer4, 0, rec.source_size);
	uint8_t buffer5[rec.source_size];
	memset(buffer5, 0, rec.source_size);
	uint8_t buffer_aux[rec.coded_size];
	memset(buffer_aux, 0, rec.coded_size);


	uint8_t pkt_store[num_pkts * 4 * enc.coded_size];
	memset(pkt_store, 0, num_pkts * 4 * enc.coded_size);
	int coded_pkts = 0;
	int store_idx = 0;

	for (int i = 0; i < num_pkts; i++) {
		skb_new(&packet, buffer, sizeof(buffer));
		skb_reserve(&packet, sizeof(uint32_t) + sizeof(uint16_t));
		for (int j = 0; skb_tailroom(&packet) >= sizeof(uint8_t); j++) {
			skb_put_u8(&packet, (uint8_t) rand());
		}
		seq_num++;
		skb_push_u32(&packet, seq_num);
		skb_push_u16(&packet, seq_gen_num);
		nck_put_source(&enc, &packet);

		while (nck_has_coded(&enc)) {
			skb_new(&packet2, buffer2, enc.coded_size);
			nck_get_coded(&enc, &packet2);
			memcpy(pkt_store + (store_idx * enc.coded_size), packet2.data, packet2.len);
			coded_pkts++;
			store_idx++;
		}
	}
	uint8_t buffer_order[coded_pkts];
	memset(buffer_order, 0, coded_pkts);
	for (int i = 0; i< coded_pkts; i++) buffer_order[i] = i;
	printf("\n");
	for (int l = 0; l < coded_pkts; l++) {
		printf("%*d\t",3, buffer_order[l]+1);
	}
	printf("\n");
	shuffle(buffer_order, coded_pkts);
	for (int l = 0; l < coded_pkts; l++) {
		printf("%*d\t",3, buffer_order[l] + 1);
	}
	printf("\n");
	for (int l = 0; l < coded_pkts; l++) {
		printf("%*d\t",3, (buffer_order[l]/4)+1);
	}
	printf("\n");
	store_idx = 0;
	for (int i = 0; i < coded_pkts; i++) {
		nck_schedule_run(&schedule, &step);
		skb_new(&packet2, buffer2, rec.coded_size);
		skb_put(&packet2, rec.coded_size);
		memcpy(packet2.data, pkt_store + (buffer_order[store_idx] * rec.coded_size), rec.coded_size);
		store_idx++;
		nck_put_coded(&rec, &packet2);

		while (nck_has_coded(&rec)) {
			skb_new(&packet3, buffer3, rec.coded_size);
			nck_get_coded(&rec, &packet3);

			nck_put_coded(&rec2, &packet3);

		}

		while (nck_has_source(&rec)) {
			skb_new(&packet4, buffer4, rec.source_size);
			nck_get_source(&rec, &packet4);
			uint16_t gen = skb_pull_u16(&packet4);
			uint32_t seq = skb_pull_u32(&packet4);
			UNUSED(gen);
			printf("GOT SRC - Seq %d \n", seq);
		}

		while (nck_has_source(&rec2)) {
			skb_new(&packet5, buffer5, rec.source_size);
			nck_get_source(&rec2, &packet5);
			uint16_t gen = skb_pull_u16(&packet5);
			uint32_t seq = skb_pull_u32(&packet5);
			UNUSED(gen);
			printf("GOT SRC Rec2 -  Seq %d \n", seq);
		}

	}
	nck_flush_source(&rec2);
	while (nck_has_source(&rec2)) {
		skb_new(&packet5, buffer5, rec.source_size);
		nck_get_source(&rec2, &packet5);
		uint16_t gen = skb_pull_u16(&packet5);
		uint32_t seq = skb_pull_u32(&packet5);
		UNUSED(gen);
		printf("GOT SRC Rec2 -  Seq %d \n", seq);
	}

	nck_free(&enc);
	nck_free(&rec);
	nck_free(&rec2);
	nck_schedule_free_all(&schedule);

	return 0;
}
