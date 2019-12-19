#include <arpa/inet.h>
#include <nckernel/skb.h>
#include <stdint.h>
#include "../private.h"
#include "packet.h"
#include "common.h"

struct kodo_header {
	uint32_t seqno;
	uint8_t systematic_flag;
} __packed;

static char *nck_sw_common_describe_coded_packet(struct sk_buff *packet, int symbols)
{
	static char debug[4096];
	struct sw_coded_packet *sw_coded_packet;
	struct kodo_header *kodo_header;
	uint8_t *coefficients;

	int i, len = sizeof(debug), pos = 0;

	debug[0] = 0;

	sw_coded_packet = (struct sw_coded_packet *) packet->data;
	kodo_header = (struct kodo_header *) (sw_coded_packet + 1);
	coefficients = (uint8_t *) (kodo_header + 1);

	if (packet->len < sizeof(*sw_coded_packet) + sizeof(kodo_header) + symbols)
		return (char *)"\"error\":\"too short coded packet\"";

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"packetno\":% 5d, \"seqno\":% 5d, ",
			ntohs(sw_coded_packet->packet_no),
			ntohl(kodo_header->seqno));

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"flags\": { \"feedback_requested\": %s, \"flush\": %s }, ",
			(sw_coded_packet->flags & SW_CODED_PACKET_FEEDBACK_REQUESTED) ? "true" : "false",
			(sw_coded_packet->flags & SW_CODED_PACKET_FLUSH) ? "true" : "false");

	switch (kodo_header->systematic_flag) {
	case 0xff:
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"coded\":false");
		break;
	case 0x00:
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"coded\":true, \"coefficients\":\"%02x",
				coefficients[0]);

		for (i = 1; i < symbols && pos < len; i++)
			pos += snprintf(&debug[pos], CHK_ZERO(len - pos), " %02x", coefficients[i]);

		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"");
		break;
	default:
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"error\":\"unknown data type\"");
		break;

	}

	debug[sizeof(debug) - 1] = 0;

	return debug;
}

static char *nck_sw_common_describe_feedback_packet(struct sk_buff *packet, int symbols)
{
	struct sw_feedback_packet *sw_feedback_packet;
	uint8_t *bitmap;
	static char debug[4096];
	int i, len = sizeof(debug), pos = 0;

	debug[0] = 0;

	sw_feedback_packet = (struct sw_feedback_packet *)packet->data;
	bitmap = (uint8_t *) (sw_feedback_packet + 1);

	if (packet->len != sizeof(*sw_feedback_packet) + DIV_ROUND_UP(symbols, 8))
		return (char *)"\"error\":\"feedback packet with not matching length\"";

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"packetno\":% 5d, \"seqno\":% 5d, \"feedback_no\": % 5d, \"feedback\":true, \"bitmap\":\"",
			ntohs(sw_feedback_packet->packet_no), ntohl(sw_feedback_packet->sequence), ntohs(sw_feedback_packet->feedback_no));

	for (i = 0; i < symbols && pos < len; i++) {
		pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "%c",
				(bitmap[i/8] & (1 << (i % 8))) ? '1' : '0');
	}

	pos += snprintf(&debug[pos], CHK_ZERO(len - pos), "\"");

	debug[sizeof(debug) - 1] = 0;

	return debug;
}

EXPORT
char *nck_sw_common_describe_packet(struct sk_buff *packet, int symbols)
{
	uint8_t packet_type = packet->data[0];

	if (packet->len < 1)
		return (char *)"\"error\":\"too short packet\"";

	switch (packet_type) {
	case SW_PACKET_TYPE_CODED:
		return nck_sw_common_describe_coded_packet(packet, symbols);
		break;
	case SW_PACKET_TYPE_FEEDBACK:
		return nck_sw_common_describe_feedback_packet(packet, symbols);
		break;
	}

	return (char *)"\"error\":\"unknown data type\"";
}
