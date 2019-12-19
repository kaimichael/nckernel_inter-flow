#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <assert.h>
#include <getopt.h>


#include <nckernel/nckernel.h>
#include <nckernel/api.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

int num_clients = 8;

struct path {
	const char *name;

	double loss;
	int bps;
	struct timeval latency;
	double max_jitter;

	struct nck_timer *timer;
	int ready;
	struct nck_timer_entry *ready_timer;
};

struct frame
{
	struct nck_encoder *encoder;
	struct nck_recoder *rec;
	struct nck_recoder **rec_array;

	struct nck_schedule *schedule;
	struct path *path;
	struct timeval send;
	struct timeval receive;
	int success;

	const char *pkt_desc;
	const char *coder_desc;

	struct incom_packet *new_pkt;

	uint8_t buffer[];
};

struct incom_packet
{
	struct sk_buff packet;
	int must_recode;
};

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
		      int bps, double loss, int latency_ms, double max_jitter_ms)
{
	path->name = name;
	path->bps = bps;
	path->loss = loss;
	path->ready = 1;
	path->timer = timer;
	path->max_jitter = max_jitter_ms;

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

static struct frame * new_frame(struct nck_schedule *sched, struct path *path,
				struct nck_encoder *enc, struct nck_recoder *rec,
				struct nck_recoder *rec_array, size_t size)
{
	struct frame *result;

	result = malloc(sizeof(*result) + size);
	result->path = path;
	result->schedule = sched;
	result->send = sched->time;
	timerclear(&result->receive);
	result->success = 0;
	if(strcmp(path->name, "cellular") == 0)
	{
		result->encoder = enc;
		result->rec = rec;
		result->rec_array = NULL;
	}
	else if(strcmp(path->name, "multicast") == 0){
		result->encoder = NULL;
		result->rec = rec;
		result->rec_array = malloc(num_clients * sizeof(struct nck_recoder));
		for(int i=0; i < num_clients; i++)
			result->rec_array[i] = &rec_array[i];
	}
	result->pkt_desc = NULL;
	result->coder_desc = NULL;
	result->new_pkt = malloc(sizeof(struct incom_packet));

	skb_new(&result->new_pkt->packet, result->buffer, size);
	result->new_pkt->must_recode = 0;

	return result;
}

static int create_pkt(struct nck_encoder *enc, size_t source_size, uint32_t *pkt_number)
{
	struct sk_buff packet;
	uint8_t buffer[source_size];
	uint8_t payload[source_size - sizeof(uint32_t)];
	for (uint32_t j = 0; j < source_size - sizeof(uint32_t); j++) {
		payload[j] = (uint8_t) rand();
	}

	if (!nck_full(enc)) {

		skb_new(&packet, buffer, sizeof(buffer));
		skb_reserve(&packet, sizeof(uint32_t));

		skb_put(&packet, skb_tailroom(&packet));

		memcpy(packet.data,payload,source_size - sizeof(uint32_t));
		*pkt_number = *pkt_number + 1;

		skb_push_u32(&packet, *pkt_number);
		/*
		printf("NEW PACKET: \t");
		for (int l = 0; l < packet.len; l++) {
			printf("%02x\t", packet.data[l]);
		}
		printf("\n");
		*/
		nck_put_source(enc, &packet);
	}

	return 0;
}

static void receive_data(struct nck_timer_entry *handle, void *context, int success)
{
	struct frame *frame = (struct frame *)context;

	if (success && frame->success) {
		/*
		printf("PKT RECV CELL: \t");
		for (int l = 0; l < frame->new_pkt->packet.len; l++) {
			printf("%02x\t", frame->new_pkt->packet.data[l]);
		}
		printf("\n");
		*/
		nck_put_coded(frame->rec, &frame->new_pkt->packet);
	}

	if (handle) {
		nck_timer_free(handle);
	}
	free(frame->new_pkt);
	free(frame);
}

static void receive_data_mcast(struct nck_timer_entry *handle, void *context, int success)
{
	struct frame *frame = (struct frame *)context;
	struct sk_buff pkt_aux;

	if (success && frame->success) {
		for(int i = 0; i < num_clients; i++)
		{
			if(frame->rec_array[i] == frame->rec) continue;
			uint8_t buff_aux[frame->rec->coded_size];
			memset(buff_aux,0,frame->rec->coded_size);
			skb_new(&pkt_aux,buff_aux,frame->rec->coded_size);
			skb_put(&pkt_aux,frame->rec->coded_size);
			for (uint32_t l = 0; l < frame->rec->coded_size; l++) {
				pkt_aux.data[l] = frame->new_pkt->packet.data[l];
			}
			nck_put_coded(frame->rec_array[i], &pkt_aux);

			if(frame->new_pkt->must_recode == 0){
				struct sk_buff packet_aux;
				uint8_t buffer_aux[frame->rec->coded_size];
				memset(buffer_aux,0,frame->rec->coded_size);
				skb_new(&packet_aux,buffer_aux,frame->rec->coded_size);
				nck_get_coded(frame->rec_array[i], &packet_aux);
			}
		}
	}

	if (handle) {
		nck_timer_free(handle);
	}
	free(frame->new_pkt);
	free(frame->rec_array);
	free(frame);
}

static void path_send(struct path *path, int len, struct frame *frame, nck_timer_callback callback)
{
	struct timeval delay,real_latency;
	uint64_t bits = (uint64_t)len * 8;
	delay.tv_sec = bits / path->bps;
	delay.tv_usec = bits * 1000000 / path->bps;

	double jitter = path->max_jitter*((drand48()*2 - 1));
	if(jitter == 0){
		real_latency = path->latency;
	}else{
		real_latency.tv_sec = path->latency.tv_sec + ((int)jitter/1000);
		real_latency.tv_usec = (int)(path->latency.tv_usec + ((jitter - (double) real_latency.tv_sec * 1000) * 1000));
	}


	frame->success = drand48() >= path->loss;
	timeradd(&frame->schedule->time, &real_latency, &frame->receive);
	timeradd(&frame->receive, &delay, &frame->receive);


	nck_timer_add(path->timer, &real_latency, frame, callback);

	path->ready = 0;
	nck_timer_rearm(path->ready_timer, &delay);
}

static void send_data(struct path *path, struct frame *frame, struct nck_encoder *enc,
		      struct nck_recoder *rec_source)
{
	if(strcmp(path->name, "cellular") == 0){
		frame->new_pkt->must_recode = 1;
		nck_get_coded(enc, &frame->new_pkt->packet);
		/*
		printf("GET CODED SERV:\t");
		for (int l = 0; l < frame->new_pkt->packet.len; l++) {
			printf("%02x\t", frame->new_pkt->packet.data[l]);
		}
		printf("\n");
		 */
		frame->pkt_desc = nck_describe_packet(enc, &frame->new_pkt->packet);
		frame->coder_desc = nck_debug(enc);
		path_send(path, frame->new_pkt->packet.len, frame, &receive_data);
	}else if(strcmp(path->name, "multicast") == 0){
		frame->new_pkt->must_recode = 0;
		nck_get_coded(rec_source, &frame->new_pkt->packet);
		/*
		printf("RECODED MCAST:\t");
		for (int l = 0; l < frame->new_pkt->packet.len; l++) {
			printf("%02x\t", frame->new_pkt->packet.data[l]);
		}
		printf("\n");
		*/
		frame->pkt_desc = nck_describe_packet(rec_source, &frame->new_pkt->packet);
		frame->coder_desc = nck_debug(rec_source);
		path_send(path, frame->new_pkt->packet.len, frame, &receive_data_mcast);
	}
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

static int is_file_decoded(int *array, int num_pkts){
	int checker = 0;
	for(int i = 0; i < num_clients; i++){
		if(array[i] == num_pkts) checker++;
	}
	if(checker == num_clients) return 1;
	else return 0;
}
void print_usage() {
	printf("Usage: coop_simulator [-v <verbose> -p <packets> -L <loss_cellular (0..1)> -l <loss_mcast (0..1)> "
		       "-R <datarate_cellular (bps)> -r <datarate_mcast (bps)> -D <delay_cellular (ms)> "
		       "-d <delay_mcast (ms)> -F <jitter_cellular (ms)> -j <jitter_mcast (ms)>]\n");
}
int main(int argc, char *argv[])
{
	srand48(time(NULL));
	struct nck_schedule sched;
	struct nck_timer timer;
	struct nck_encoder enc;
	struct path cellular, mcast;
	struct timeval step, timeout_bf;
	struct frame *frame;
	struct sk_buff packet_recv;
	size_t source_size, coded_size;

	// Data creation
	uint32_t total_pkts = 1000;
	double loss_cellular = 0;
	double loss_mcast = 0;
	int datarate_cellular = 100000000;
	int datarate_mcast = 1000000000;
	int delay_cellular = 20;
	int delay_mcast = 2;
	double max_jitter_cellular = 0;
	double max_jitter_mcast = 0;
	static int verbose = 0;

	static struct option long_options[] = {
		{"verbose",      no_argument,       0,  'v' },
		{"num_clients",      required_argument,       0,  'N' },
		{"packets",      required_argument,       0,  'p' },
		{"loss_cellular",      required_argument,       0,  'L' },
		{"loss_mcast",      required_argument,       0,  'l' },
		{"datarate_cellular",      required_argument,       0,  'R' },
		{"datarate_mcast",      required_argument,       0,  'r' },
		{"delay_cellular",      required_argument,       0,  'D' },
		{"delay_mcast",      required_argument,       0,  'd' },
		{"jitter_cellular",      required_argument,       0,  'J' },
		{"jitter_mcast",      required_argument,       0,  'j' },
		{0,           0,                 0,  0   }
	};
	int long_index =0;
	int opt = 0;
	int num_opt = 0;
	while ((opt = getopt_long(argc, argv,"vN:p:L:l:R:r:D:d:J:j:",
				  long_options, &long_index )) != -1) {
		switch (opt) {
			case 'v' :
				verbose = 1;
				num_opt++;
				break;
			case 'N' :
				num_clients = atoi(optarg);
				num_opt++;
				break;
			case 'p' :
				total_pkts = atoi(optarg);
				num_opt++;
				break;
			case 'L' :
				loss_cellular = atof(optarg);
				num_opt++;
				break;
			case 'l' :
				loss_mcast = atof(optarg);
				num_opt++;
				break;
			case 'R' :
				datarate_cellular = atoi(optarg);
				num_opt++;
				break;
			case 'r' :
				datarate_mcast = atoi(optarg);
				num_opt++;
				break;
			case 'D' :
				delay_cellular = atoi(optarg);
				num_opt++;
				break;
			case 'd' :
				delay_mcast = atoi(optarg);
				num_opt++;
				break;
			case 'J' :
				max_jitter_cellular = atoi(optarg);
				num_opt++;
				break;
			case 'j' :
				max_jitter_mcast = atoi(optarg);
				num_opt++;
				break;
			default: print_usage();
				exit(EXIT_FAILURE);
		}
	}
	struct nck_recoder *rec_array = malloc(num_clients * sizeof(struct nck_recoder));
	int msg_decoded[num_clients];
	memset(msg_decoded,0,sizeof(msg_decoded));
	__time_t ipd_decoded[num_clients];
	memset(ipd_decoded,0,sizeof(ipd_decoded));
	struct timeval start[num_clients], end[num_clients];
	memset(start, 0, sizeof(start));
	memset(end, 0, sizeof(end));

	if((max_jitter_cellular > delay_cellular) || (max_jitter_mcast > delay_mcast)){
		fprintf(stderr,"Maximum jitter must be lower than the link delay\n");
		print_usage();
		exit(EXIT_FAILURE);
	}

	nck_schedule_init(&sched);
	nck_schedule_timer(&sched, &timer);

	// Encoder and recoders creation
	if (nck_create_encoder(&enc, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create encoder");
		return -1;
	}
	assert(!nck_has_coded(&enc));
	assert(!nck_full(&enc));

	for(int i = 0; i < num_clients; i++)
	{
		if (nck_create_recoder(&rec_array[i], &timer, NULL, get_env_opt)) {
			fprintf(stderr, "Failed to create recoder");
			return -1;
		}
		assert(!nck_has_source(&rec_array[i]));
		assert(!nck_has_feedback(&rec_array[i]));
	}

	source_size = enc.source_size;
	coded_size = enc.coded_size;

	uint8_t buffer_recv[source_size];
	memset(buffer_recv, 0, sizeof(uint8_t) * source_size);
	uint8_t buffer_recod[coded_size];
	memset(buffer_recod, 0, sizeof(uint8_t) * coded_size);

	// Path creation
	path_init(&cellular, "cellular", &timer, datarate_cellular, loss_cellular, delay_cellular, max_jitter_cellular);
	path_init(&mcast, "multicast", &timer, datarate_mcast, loss_mcast, delay_mcast, max_jitter_mcast);

	uint32_t pkt_number = 0;
	int rec_dest;
	while (pkt_number != total_pkts) {

		// run all scheduled events
		if (nck_schedule_run(&sched, &step)) {
			// stop if nothing was scheduled
		}

		// Encoding
		if ((pkt_number < total_pkts) && cellular.ready) create_pkt(&enc, source_size, &pkt_number);
		while (nck_has_coded(&enc) && cellular.ready) {
			rec_dest = (pkt_number - 1) % num_clients;
			frame = new_frame(&sched, &cellular, &enc, &rec_array[rec_dest], NULL, coded_size);
			send_data(&cellular, frame, &enc, NULL);
		}


		// Recoding
		for(int i = 0; i < num_clients; i++) {
			nck_schedule_run(&sched, &step);
			while (nck_has_coded(&rec_array[i])) {
				frame = new_frame(&sched, &mcast, NULL, &rec_array[i], rec_array,
						  coded_size);
				send_data(&mcast, frame, NULL, &rec_array[i]);
			}
		}

		// Decoding
		for(int i = 0; i < num_clients; i++){
			nck_schedule_run(&sched, &step);
			while (nck_has_source(&rec_array[i])) {
				skb_new(&packet_recv, buffer_recv, rec_array[i].source_size);
				nck_get_source(&rec_array[i], &packet_recv);
				//uint32_t seq = skb_pull_u32(&packet_recv);
				//printf("GOT SRC Rec %d - Seq. %d\n",i, seq);
				end[i] = sched.time;
				ipd_decoded[i] += ((end[i].tv_sec - start[i].tv_sec) * 1000000 + (end[i].tv_usec - start[i].tv_usec));
				start[i] = sched.time;
				msg_decoded[i]++;
			}
		}

		if(is_file_decoded(msg_decoded, total_pkts)) {
			break;
		}
		if(step.tv_sec > 5){
			timeout_bf = sched.time;
		}

		timeradd(&sched.time, &step, &sched.time);

	}

	// Writing to console/file
	float tot_loss_ratio = 0;
	float ipd_total = 0;
	int msg_decoded_total = 0;
	for (int i = 0; i < num_clients; i++){
		ipd_total += ipd_decoded[i];
		msg_decoded_total += msg_decoded[i];
	}
	ipd_total = ipd_total/msg_decoded_total;
	if(is_file_decoded(msg_decoded, total_pkts)) {
		if(verbose){
			printf("Full message decoded.\n");
			printf("Loss ratio:");
			for(int i = 0; i < num_clients; i++)
			{
				printf("\tRec. %d: %.2f%%", i,((total_pkts - (float)msg_decoded[i])/total_pkts)*100);
				tot_loss_ratio += ((total_pkts - (float)msg_decoded[i])/total_pkts)*100;
			}
			printf("\n");
			printf("Total Loss Ratio: %.2f%%\n", tot_loss_ratio/num_clients);

			printf("Time to transfer the packets: %ld.%06ld seconds\n",
			       sched.time.tv_sec, sched.time.tv_usec);
			printf("The mean inter packet delay among all clients is %.2f microsecs. \n", ipd_total);
		} else {
			printf("1");
			for(int i = 0; i < num_clients; i++)
			{
				printf("\t%.2f", ((total_pkts - (float)msg_decoded[i])/total_pkts)*100);
				tot_loss_ratio += ((total_pkts - (float)msg_decoded[i])/total_pkts)*100;
			}
			printf("\t%.2f\t%ld.%06ld\t%.2f", tot_loss_ratio/num_clients,sched.time.tv_sec, sched.time.tv_usec,ipd_total);
		}
	}else{
		if(verbose){
			printf("Errors detected while sending. Waiting for timeout to flush coders.\n");
			printf("Loss ratio:");
			for(int i = 0; i < num_clients; i++)
			{
				printf("\tRec. %d: %.2f%%", i,((total_pkts - (float)msg_decoded[i])/total_pkts)*100);
				tot_loss_ratio += ((total_pkts - (float)msg_decoded[i])/total_pkts)*100;
			}
			printf("\n");
			printf("Total Loss Ratio: %.2f%%\n", tot_loss_ratio/num_clients);

			printf("Time to transfer the packets after timeout: %ld.%06ld seconds\n",
			       sched.time.tv_sec, sched.time.tv_usec);
			printf("Time to transfer the packets before timeout: %ld.%06ld seconds\n",
			       timeout_bf.tv_sec, timeout_bf.tv_usec);
			printf("The mean inter packet delay among all clients is %.2f microsecs. \n", ipd_total);
		} else {
			printf("0");
			for(int i = 0; i < num_clients; i++)
			{
				printf("\t%.2f", ((total_pkts - (float)msg_decoded[i])/total_pkts)*100);
				tot_loss_ratio += ((total_pkts - (float)msg_decoded[i])/total_pkts)*100;
			}
			printf("\t%.2f\t%ld.%06ld\t%ld.%06ld\t%.2f", tot_loss_ratio/num_clients,sched.time.tv_sec,
			       sched.time.tv_usec, timeout_bf.tv_sec, timeout_bf.tv_usec, ipd_total);
		}

	}

	printf("\n");

	// Memory free
	nck_free(&enc);
	for(int i = 0; i < num_clients; i++) nck_free(&rec_array[i]);
	free(rec_array);
	path_free(&cellular);
	path_free(&mcast);
	nck_schedule_free_all(&sched);

	return 0;

}
