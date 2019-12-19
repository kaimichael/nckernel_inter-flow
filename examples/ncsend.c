// standard IO includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// includes for UDP sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

// includes for nckernel
#include <nckernel/nckernel.h>
#include <nckernel/skb.h>

int main()
{
	int sock;
	struct nck_encoder enc;

	// configuration values for the encoder
	struct nck_option_value options[] = {
		{ "protocol", "noack" },
		{ "symbol_size", "1400" },
		{ "symbols", "4" },
		{ "redundancy", "1" },
		{ NULL, NULL }
	};

	// initialize the encoder according to the configuration
	if (nck_create_encoder(&enc, NULL, options, nck_option_from_array)) {
		fprintf(stderr, "Failed to create encoder");
		return -1;
	}

	// create a new socket
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Could not create socket");
		return -1;
	}

	// setup the destination address
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET; // IPv4
	addr.sin_port = htons(12345); // destination port
	inet_aton("127.0.0.1", &addr.sin_addr);

	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr))) {
		perror("Failed to connect");
		return -1;
	}

	// read input until the stream is closed
	while (!feof(stdin)) {
		// Reserve space for reading.
		// The size should be bigger than the symbol size of the encoder.
		// The symbol size can also be accessed with ``enc.source_size``.
		uint8_t source[1400];

		// the socket buffer structure is used for passing the data from and to the encoder
		struct sk_buff skb;

		// we initialize it with the underlying memory buffer
		skb_new(&skb, source, sizeof(source));

		// we read the data into the tail space of the socket buffer
		char *line = fgets((char*)skb.tail, skb_tailroom(&skb), stdin);
		if (!line) {
			// end of file reached
			return 0;
		}

		// mark the actually used tail space as taken with the put command
		skb_put(&skb, strlen(line));

		// pass the data to the encoder
		nck_put_source(&enc, &skb);

		// we check if the encoder has coded data and send everything out
		while (nck_has_coded(&enc)) {
			// We reserve some more space for the coded packet.
			// This should have enough space for all coded packets.
			// The size of a coded packet cann be accessed with ``enc.coded_size``.
			uint8_t coded[1500];

			// again we initialize a socket buffer
			skb_new(&skb, coded, sizeof(coded));

			// this time we give this buffer empty to the encoder to fill it
			nck_get_coded(&enc, &skb);

			// we send out the packet with our socket
			if (send(sock, skb.data, skb.len, 0) < 0) {
				perror("Error while sending");
			}
		}
	}

	return 0;
}
