#ifndef __packed
#define __packed __attribute((packed))   /* linux kernel compat */
#endif

/**
 * enum sw_packet_type - various packet types
 * @SW_PACKET_TYPE_CODED: systematic or coded packet, contains information from a single symbol (systematic)
 *    or linear combination of symbols (coded). Sent by encoders and recoders.
 * @SW_PACKET_TYPE_FEEDBACK: feedback packet to report the current decoding status. Sent
 *    by decoders and recoders.
 */
enum sw_packet_type {
	SW_PACKET_TYPE_CODED = 1,
	SW_PACKET_TYPE_FEEDBACK = 2,
};

/**
 * sw_coded_packet - sliding window coded packet or systematic packet
 * @packet_type: either SW_PACKET_TYPE_CODED or SW_PACKET_TYPE_SYSTEMATIC
 * @order: window size is 2^order
 * @flags: see enum sw_coded_packet_flags
 * @reserved: reserved byte for future use (for padding)
 * @packet_no: incremental packet counter
 */
struct sw_coded_packet {
	uint8_t packet_type;
	uint8_t order;
	uint8_t flags;
	uint8_t reserved;
	uint16_t packet_no;
} __packed;

/**
 * sw_coded_packet_flags - flags sent by the encoder
 * @SW_CODED_PACKET_FEEDBACK_REQUESTED: requests a feedback from the decoder
 */
enum sw_coded_packet_flags {
	SW_CODED_PACKET_FEEDBACK_REQUESTED = 0x01,
	SW_CODED_PACKET_FLUSH = 0x02,
};

/**
 * sw_feedback_packet - sliding window feedback packet
 * @packet_type: should be set to SW_PACKET_TYPE_FEEDBACK
 * @order: window size is 2^order
 * @packet_no: number of the last received packet_no
 * @feedback_no: incremental number of the feedback
 * @reserved: currently unused fields
 * @sequence: latest sequence number of the decoder
 * @first_missing: sequence number of the first missing packet
 */
struct sw_feedback_packet {
	uint8_t packet_type;
	uint8_t order;
	uint16_t packet_no;
	uint16_t feedback_no;
	uint16_t reserved;
	uint32_t sequence;
	uint32_t first_missing;
} __packed;

