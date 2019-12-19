// standard IO includes
#include <stdio.h>
#include <stdlib.h>

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
	struct nck_decoder dec;

	// configuration values for the decoder
	struct nck_option_value options[] = {
		{ "protocol", "noack" },
		{ "symbol_size", "1400" },
		{ "symbols", "4" },
		{ NULL, NULL }
	};

	// initialize the decoder according to the configuration
	if (nck_create_decoder(&dec, NULL, options, nck_option_from_array)) {
		fprintf(stderr, "Failed to create decoder");
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

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr))) {
		perror("Failed to connect");
		return -1;
	}

	while (1) {
		// Reserve space for receiving.
		// The size should be bigger than the coded packet size.
		// The symbol size can also be accessed with ``dec.coded_size``.
		uint8_t coded[1500];
		//
		// the socket buffer structure is used for passing the data from and to the decoder
		struct sk_buff skb;

		// we initialize it with the underlying memory buffer
		skb_new(&skb, coded, sizeof(coded));

		// we read the data into the tail space of the socket buffer
		ssize_t len = recv(sock, skb.tail, skb_tailroom(&skb), 0);
		if (len < 0) {
			perror("Error receiving packet");
			continue;
		}

		// mark the actually used tail space as taken with the put command
		skb_put(&skb, len);

		// pass the data to the decoder
		nck_put_coded(&dec, &skb);

		// we check if the decoder has decoded packets
		while (nck_has_source(&dec)) {
			// We reserve some more space for the decoded packet.
			// This should have enough space for all source packets.
			// The size of a coded packet cann be accessed with ``dec.source_size``.
			uint8_t source[1500];

			// again we initialize a socket buffer
			skb_new(&skb, source, sizeof(source));

			// this time we give this buffer empty to the decoder to fill it
			nck_get_source(&dec, &skb);

			// we write the decoded payload to the output
			fprintf(stdout, "%s", skb.data);
		}
	}

	return 0;
}
