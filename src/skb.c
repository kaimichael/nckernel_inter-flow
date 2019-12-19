#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include <nckernel/skb.h>

#include "private.h"

void skb_new(struct sk_buff *skb, uint8_t *buffer, unsigned total_len)
{
	skb->head = buffer;
	skb->data = buffer;
	skb->tail = buffer;
	skb->end = buffer+total_len;
	skb->len = 0;
}

void skb_new_clone(struct sk_buff *skb, uint8_t *buffer, const struct sk_buff *oldskb)
{
	unsigned int headroom;
	unsigned int total_len;

	headroom = skb_headroom(oldskb);
	total_len = oldskb->end - oldskb->head;

	skb->len = oldskb->len;
	skb->head = buffer;
	skb->data = buffer + headroom;
	skb->tail = buffer + headroom + oldskb->len;
	skb->end = buffer + total_len;
}

void skb_reserve(struct sk_buff *skb, unsigned header_len)
{
	// reserve should only be called on empty SKBs
	assert(skb->data == skb->tail);
	assert(skb->len == 0);
	assert(skb->data + header_len <= skb->end);

	skb->data += header_len;
	skb->tail += header_len;
}

unsigned skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

unsigned skb_tailroom(const struct sk_buff *skb)
{
	return skb->end - skb->tail;
}

int pskb_may_pull(const struct sk_buff *skb, unsigned len)
{
	return len <= skb->len;
}

void *skb_push(struct sk_buff *skb, unsigned len)
{
	assert(skb_headroom(skb) >= len);

	skb->data -= len;
	skb->len += len;

	return skb->data;
}

void *skb_pull(struct sk_buff *skb, unsigned len)
{
	assert(pskb_may_pull(skb, len));

	skb->data += len;
	skb->len -= len;

	return skb->data;
}

void *skb_put(struct sk_buff *skb, unsigned len)
{
	assert(skb_tailroom(skb) >= len);

	uint8_t *tmp = skb->tail;
	skb->tail += len;
	skb->len += len;
	return tmp;
}

void skb_trim(struct sk_buff *skb, unsigned len)
{
	assert(skb->len >= len);

	skb->tail -= len;
	skb->len -= len;
}

void skb_push_u8(struct sk_buff *skb, uint8_t value)
{
	uint8_t *buffer = skb_push(skb, sizeof(value));
	*buffer = value;
}

void skb_push_u16(struct sk_buff *skb, uint16_t value)
{
	void *buffer = skb_push(skb, sizeof(value));
	value = htons(value);
	memcpy(buffer, &value, sizeof(value));
}

void skb_push_u32(struct sk_buff *skb, uint32_t value)
{
	void *buffer = skb_push(skb, sizeof(value));
	value = htonl(value);
	memcpy(buffer, &value, sizeof(value));
}

void skb_put_u8(struct sk_buff *skb, uint8_t value)
{
	uint8_t *buffer = skb_put(skb, sizeof(value));
	*buffer = value;
}

void skb_put_u16(struct sk_buff *skb, uint16_t value)
{
	void *buffer = skb_put(skb, sizeof(value));
	value = htons(value);
	memcpy(buffer, &value, sizeof(value));
}

void skb_put_u32(struct sk_buff *skb, uint32_t value)
{
	void *buffer = skb_put(skb, sizeof(value));
	value = htonl(value);
	memcpy(buffer, &value, sizeof(value));
}

uint8_t skb_pull_u8(struct sk_buff *skb)
{
	uint8_t *buffer = skb->data;
	skb_pull(skb, sizeof(*buffer));
	return *buffer;
}

uint16_t skb_pull_u16(struct sk_buff *skb)
{
	uint16_t value;
	uint8_t *buffer = skb->data;

	skb_pull(skb, sizeof(value));
	memcpy(&value, buffer, sizeof(value));

	return ntohs(value);
}

uint32_t skb_pull_u32(struct sk_buff *skb)
{
	uint32_t value;
	uint8_t *buffer = skb->data;

	skb_pull(skb, sizeof(uint32_t));
	memcpy(&value, buffer, sizeof(value));

	return ntohl(value);
}

void skb_trim_zeros(struct sk_buff *skb)
{
	for (unsigned i = skb->len-1; i > 0; --i) {
		if (skb->data[i]) {
			skb->len = i + 1;
			skb->tail = skb->data + skb->len;
			return;
		}
	}
}

void skb_put_zeros(struct sk_buff *skb, unsigned total_len)
{
	if (skb->len >= total_len) {
		return;
	}

	unsigned append = total_len - skb->len;
	memset(skb_put(skb, append), 0, append);
}

const char *skb_str(const struct sk_buff *skb)
{
	static char buffer[256]; // TODO: maybe choose a smaller buffer size
	unsigned int i;
	int pos;

	pos = snprintf(buffer, sizeof(buffer),
			"packet %p headroom=%u len=%u tailroom=%u:",
			(void*)skb, skb_headroom(skb), skb->len, skb_tailroom(skb));

	for (i = 0; i+3 < skb->len && pos+20 < (int)sizeof(buffer); i += 4) {
		pos += snprintf(buffer+pos, CHK_ZERO((int)sizeof(buffer)-pos),
				"  %02x %02x %02x %02x",
				skb->data[i], skb->data[i+1], skb->data[i+2], skb->data[i+3]);
	}

	pos += snprintf(buffer+pos, CHK_ZERO((int)sizeof(buffer)-pos), " ");

	for (; i < skb->len && pos+9 < (int)sizeof(buffer); ++i) {
		pos += snprintf(buffer+pos, CHK_ZERO((int)sizeof(buffer)-pos),
				" %02x", skb->data[i]);
	}

	if (i < skb->len) {
		pos += snprintf(buffer+pos, CHK_ZERO((int)sizeof(buffer)-pos), " ...");
	}

	buffer[pos] = 0;

	return buffer;
}

void skb_print(FILE *file, const struct sk_buff *skb)
{
	skb_print_part(file, skb, skb->len, 8, 16);
}

void skb_print_part(FILE *file, const struct sk_buff *skb, unsigned bytes, unsigned bytes_per_block, unsigned blocks_per_line)
{
	size_t i;
	size_t bytes_per_line = bytes_per_block * blocks_per_line;

	if (file == NULL) {
		return;
	}

	fprintf(file, "packet %p headroom=%u len=%u tailroom=%u",
			(void*)skb, skb_headroom(skb), skb->len, skb_tailroom(skb));

	for (i = 0; i < skb->len && i < bytes; ++i) {
		if (i % bytes_per_line == 0) {
			fprintf(file, "\n");
		} else if (i % bytes_per_block == 0) {
			fprintf(file, " ");
		}
		fprintf(file, "%02x ", skb->data[i]);
	}

	if (bytes < skb->len) {
		fprintf(file, "...");
	}
	fprintf(file, "\n");
}
