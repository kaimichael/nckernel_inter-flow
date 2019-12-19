#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/ip.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <nckernel/nckernel.h>
#include <nckernel/skb.h>
#include <nckernel/timer.h>

#define MAX_PATHS 32

struct path {
	const char *ip;
	int fd;

	struct timeval interval;
	struct nck_timer_entry *timer;
	int ready;
};

static void path_ready(struct nck_timer_entry *entry, void *context, int success)
{
	(void)entry; // unused parameter

	struct path *path = (struct path *)context;
	if (success) {
		path->ready = 1;
	}
}

static int send_packet(struct nck_recoder *rec, int writer, size_t coded_size)
{
	struct sk_buff packet;
	uint8_t buffer[coded_size];

	skb_new(&packet, buffer, coded_size);

	if (nck_get_coded(rec, &packet)) {
		return -1;
	}

	return write(writer, packet.data, packet.len);
}

static void recv_feedback(struct nck_recoder *rec, int feedback, size_t feedback_size)
{
	struct sk_buff packet;
	uint8_t buffer[feedback_size];
	ssize_t len;

	len = read(feedback, buffer, feedback_size);
	if (len < 0) {
		perror("read");
		return;
	}

	skb_new(&packet, buffer, sizeof(buffer));
	skb_put(&packet, len);

	nck_put_feedback(rec, &packet);
}

static ssize_t send_feedback(struct nck_recoder *rec, int sock, const struct sockaddr_in *addr, socklen_t addr_len, size_t feedback_size)
{
	struct sk_buff packet;
	uint8_t buffer[feedback_size];

	skb_new(&packet, buffer, feedback_size);
	if (nck_get_feedback(rec, &packet)) {
		return -1;
	}

	if (sendto(sock, packet.data, packet.len, 0, (struct sockaddr *)addr, addr_len) < 0) {
		perror("sendto");
		return -1;
	}

	return 0;
}

static int recv_packet(struct nck_recoder *rec, int reader, struct sockaddr *addr, socklen_t *addr_len, size_t coded_size)
{
	ssize_t len;
	struct sk_buff packet;
	uint8_t buffer[coded_size];

	len = recvfrom(reader, buffer, coded_size, 0, addr, addr_len);
	if (len < 0) {
		perror("recvfrom");
		return -1;
	}

	skb_new(&packet, buffer, sizeof(buffer));
	skb_put(&packet, len);
	return nck_put_coded(rec, &packet);
}

static int mainloop(struct nck_schedule *schedule, struct nck_recoder *rec, int reader, struct path *paths)
{
	struct timespec clock;
	int nfds = 0, r = 0;
	fd_set rfds, wfds;
	int keep_running = 1;
	struct timeval timeout;
	struct path *path;
	struct sockaddr_in addr = {0};
	socklen_t addr_len = 0;

	FD_ZERO(&rfds);
	FD_SET(reader, &rfds);
	nfds = reader + 1;

	FD_ZERO(&wfds);
	for (path = paths; path->fd != 0; ++path) {
		FD_SET(path->fd, &wfds);
		FD_SET(path->fd, &rfds);
		if (nfds <= path->fd) {
			nfds = path->fd + 1;
		}
	}

	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &clock);
		schedule->time.tv_sec = clock.tv_sec;
		schedule->time.tv_usec = clock.tv_nsec / 1000;

		if (nck_schedule_run(schedule, &timeout)) {
			timeout.tv_sec = 120;
			timeout.tv_usec = 0;
			keep_running = 0;
		}

		r = select(nfds, &rfds, NULL, NULL, &timeout);
		if (r == -1) {
			perror("select");
			return -1;
		} else if (r == 0 && !keep_running) {
			return 0;
		}

		keep_running = 1;

		if (FD_ISSET(reader, &rfds)) {
			addr_len = sizeof(addr);
			recv_packet(rec, reader, (struct sockaddr *)&addr, &addr_len, rec->coded_size);
		} else {
			FD_SET(reader, &rfds);
		}

		for (path = paths; path->fd != 0; ++path) {
			if (!FD_ISSET(path->fd, &rfds)) {
				FD_SET(path->fd, &rfds);
			} else {
				recv_feedback(rec, path->fd, rec->feedback_size);
			}

			if (!FD_ISSET(path->fd, &wfds)) {
				if (path->ready) {
					FD_SET(path->fd, &wfds);
				}
			} else if (nck_has_coded(rec)) {
				send_packet(rec, path->fd, rec->coded_size);
				FD_CLR(path->fd, &wfds);
				path->ready = 0;
				nck_timer_rearm(path->timer, &path->interval);
			}
		}

		if (nck_has_feedback(rec) && addr_len > 0) {
			send_feedback(rec, reader, &addr, addr_len, rec->feedback_size);
		}

		if (!nck_has_coded(rec)) {
			FD_ZERO(&wfds);
		}
	}

	return 0;
}

static int create_recv_socket(const char *port)
{
	unsigned short portnum;
	int sock;
	struct sockaddr_in addr = {0};
	char dummy;

	if (sscanf(port, "%hu%c", &portnum, &dummy) != 1) {
		fprintf(stderr, "Invalid port: %s\n", port);
		return -1;
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(portnum);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("bind");
		return -1;
	}

	return sock;
}

static int create_send_socket(const char *ip, const char *port) {
	struct addrinfo hints;
	struct addrinfo *info = NULL, *addr;
	int sock, r;

	if (ip == NULL || port == NULL) {
		fprintf(stderr, "Target IP and port need to be specified\n");
		return -1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	r = getaddrinfo(ip, port, &hints, &info);
	if (r != 0) {
		fprintf(stderr, "Can't create sender socket: %s\n", gai_strerror(r));
		return 1;
	}

	for (addr = info; addr != NULL; addr = addr->ai_next) {
		sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

		if (sock == -1) {
			continue;
		}

		if (connect(sock, addr->ai_addr, addr->ai_addrlen) != -1) {
			/* successfully connected */
			break;
		}

		/* connection didn't work, so we clean up the socket */
		close(sock);
	}

	if (info != NULL) {
		freeaddrinfo(info);
	}

	if (addr == NULL) {
		fprintf(stderr, "Can't connect to endpoint %s:%s\n", ip, port);
		return -1;
	}

	return sock;
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

	return getenv(envname);
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s LOCAL_PORT REMOTE_PORT IP...\n", name);
}

int main(int argc, char *argv[])
{
	struct nck_recoder rec;
	struct nck_schedule schedule;
	struct nck_timer timer;
	struct timespec clock;
	int reader, writer;
	struct path paths[MAX_PATHS+1];
	int i;

	if (argc < 4) {
		usage(argv[0]);
		return -1;
	}

	if (argc-3 > MAX_PATHS) {
		fprintf(stderr, "Only up to %d destinations are supported", MAX_PATHS);
		usage(argv[0]);
		return -1;
	}

	nck_schedule_init(&schedule);
	clock_gettime(CLOCK_MONOTONIC, &clock);
	schedule.time.tv_sec = clock.tv_sec;
	schedule.time.tv_usec = clock.tv_nsec / 1000;
	nck_schedule_timer(&schedule, &timer);

	if (nck_create_recoder(&rec, &timer, NULL, get_env_opt)) {
		fprintf(stderr, "Failed to create recoder");
		return -1;
	}

	reader = create_recv_socket(argv[1]);
	if (reader < 0) {
		fprintf(stderr, "Could not create listening socket\n");
		return -1;
	}

	for (i = 0; i < argc-3; ++i) {
		writer = create_send_socket(argv[i+3], argv[2]);
		if (writer < 0) {
			return -1;
		}

		paths[i] = (struct path) {
			.ip = argv[i+3],
			.fd = writer,
			.interval = (struct timeval){ .tv_sec = 0, .tv_usec = 100 },
			.timer = nck_timer_add(&timer, NULL, &paths[i], path_ready),
			.ready = 1
		};
	}
	paths[i].fd = 0;

	mainloop(&schedule, &rec, reader, paths);
}

