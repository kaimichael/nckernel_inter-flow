#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <nckernel/skb.h>
#include <nckernel/segment.h>

#define BUFFER_SIZE 1000

// we use this to store all the generated packets
struct list {
	struct list *next;
	size_t len;
	uint8_t data[];
};

// this is the header structure of our packets
// this is purely user defined and not required by the segmenter
struct header {
	uint32_t sequence;
	uint32_t remaining;
};

static struct list *segment(size_t size)
{
	uint8_t input[BUFFER_SIZE+sizeof(struct header)];

	struct list *head = NULL;
	struct list **tail = &head;

	struct nck_seg seg;
	struct sk_buff skb;
	struct header *header;
	uint32_t count;

	memset(input, 'a', sizeof(input));

	// initialize the segmentation
	nck_seg_new(&seg, input, sizeof(input), sizeof(*header));

	// loop until our segment is empty
	for (count = 0; seg.len; ++count) {
		// create a new packet with data and enough space for the header
		nck_seg_pull(&seg, &skb, size);

		// now skb contains the raw data and has some space for the header
		// we fill the header using the skb API
		header = (struct header *)skb_push(&skb, sizeof(*header));
		// we set the fields for the header in network byte order
		header->sequence = htonl(count);
		header->remaining = htonl(seg.len);

		skb_print(stdout, &skb);

		// we simulate sending by appending it to our list
		*tail = malloc(sizeof(struct list) + skb.len);
		(*tail)->len = skb.len;
		(*tail)->next = NULL;
		memcpy((*tail)->data, skb.data, skb.len);
		tail = &(*tail)->next;
	}

	return head;
}

static void reconstruct(struct list *packets)
{
	struct nck_seg seg;
	struct sk_buff skb;
	struct header *header;

	uint8_t output[BUFFER_SIZE];

	// now we try to reconstruct the original segment
	nck_seg_restore(&seg, output, sizeof(output));

	while (packets != NULL) {
		// load the packet
		skb.len = packets->len;
		skb.data = packets->data;

		// retrieve the header from the packet
		header = (struct header *)skb.data;
		skb_pull(&skb, sizeof(*header));

		// push the packet into the segment
		nck_seg_push(&seg, &skb);

		if (header->remaining == 0) {
			if (write(fileno(stdout), output, seg.len) < 0) {
				perror("write");
				exit(1);
			} else {
				fprintf(stdout, "\n");
			}
		}

		packets = packets->next;
	}
}

int main(int argc, char *argv[])
{
	char dummy;
	size_t size;
	struct list *packets;

	if (argc == 1) {
		size = 100;
	} else if (argc == 2) {
		if (1 != sscanf(argv[1], "%zu%c", &size, &dummy)) {
			fprintf(stderr, "usage: %s SIZE", argv[0]);
		}
	} else {
		fprintf(stderr, "usage: %s SIZE", argv[0]);
		exit(1);
	}

	packets = segment(size);

	reconstruct(packets);
}

