#include <cutest.h>
#undef NDEBUG
#include <assert.h>
#include <nckernel/nckernel.h>
#include <nckernel/skb.h>

#define TEST_ASSERT(cond) assert(TEST_CHECK(cond))
#define TEST_ASSERT_(cond, ...) assert(TEST_CHECK_(cond, __VA_ARGS__))

#define for_each_protocol(index) \
	for (index = 0; nck_protocol_name(index); ++index)

int skip_protocol(const char *name) {
	return getenv("NCK_PROTOCOL") != NULL && strcmp(getenv("NCK_PROTOCOL"), name) != 0;
}

void test_create()
{
	int index;
	struct nck_decoder decoder;
	const char *protocol;

	struct nck_option_value options[] = {
		{ "protocol", NULL },
		{ NULL, NULL }
	};

	for_each_protocol(index) {
		protocol = nck_protocol_name(index);
		if (skip_protocol(protocol)) {
			continue;
		}

		TEST_ASSERT_(nck_protocol_find(protocol) == index,
				"Lookup of protocol %s failed", protocol);

		options[0].value = protocol;
		TEST_ASSERT_(nck_create_decoder(&decoder, NULL, options, nck_option_from_array) == 0,
				"Decoder %s: Creation failed", protocol);
		nck_free(&decoder);
	}
}

void test_decode()
{
	int index, packetno;
	struct nck_encoder encoder;
	struct nck_decoder decoder;
	struct sk_buff skb;
	const char *protocol;
	uint8_t source[1500];
	uint8_t *coded;

	struct nck_option_value options[] = {
		{ "protocol", NULL },
		{ "symbol_size", "1500" },
		{ NULL, NULL }
	};

	for_each_protocol(index) {
		protocol = nck_protocol_name(index);
		if (skip_protocol(protocol)) {
			continue;
		}

		options[0].value = protocol;
		TEST_ASSERT_(nck_create_encoder(&encoder, NULL, options, nck_option_from_array) == 0,
				"Encoder %s: Creation failed", protocol);
		TEST_ASSERT_(nck_create_decoder(&decoder, NULL, options, nck_option_from_array) == 0,
				"Decoder %s: Creation failed", protocol);

		TEST_ASSERT_(encoder.source_size == decoder.source_size,
				"Protocol %s: Encoder and decoder have different source_size", protocol);

		TEST_ASSERT_(encoder.coded_size == decoder.coded_size,
				"Protocol %s: Encoder and decoder have different coded_size", protocol);

		TEST_ASSERT_(encoder.feedback_size == decoder.feedback_size,
				"Protocol %s: Encoder (%zu) and decoder (%zu) have different feedback_size", protocol, encoder.feedback_size, decoder.feedback_size);

		TEST_ASSERT_(nck_has_source(&decoder) == 0,
				"Decoder %s: Has a source symbol without receiving anything", protocol);

		// add packets until we get some coded packet
		for (packetno = 0; !nck_has_coded(&encoder) && packetno < 10000; ++packetno) {
			TEST_ASSERT_(nck_full(&encoder) == 0,
					"Encoder %s: Full before coded packet is available", protocol);

			skb_new(&skb, source, sizeof(source));
			snprintf((char*)skb_put(&skb, 20), 20, "packet %d", packetno);
			TEST_ASSERT_(nck_put_source(&encoder, &skb) == 0,
					"Encoder %s: Adding a source packet failed", protocol);
		}

		TEST_ASSERT_(nck_has_coded(&encoder),
				"Encoder %s: No coded packet available", protocol);

		coded = malloc(encoder.coded_size);
		while (nck_has_coded(&encoder) && !nck_has_source(&decoder)) {
			skb_new(&skb, coded, encoder.coded_size);
			TEST_ASSERT_(nck_get_coded(&encoder, &skb) == 0,
					"Encoder %s: Coding failed", protocol);
			TEST_ASSERT_(nck_put_coded(&decoder, &skb) == 0,
					"Decoder %s: Decoding failed", protocol);
		}
		free(coded);

		TEST_ASSERT_(nck_has_source(&decoder) != 0,
				"Decoder %s: Could not decode any source symbols", protocol);
		skb_new(&skb, source, sizeof(source));
		TEST_ASSERT_(nck_get_source(&decoder, &skb) == 0,
				"Decoder %s: Failed to retrieve decoded symbol", protocol);

		TEST_ASSERT_(strcmp("packet 0", (const char*)skb.data) == 0,
				"Decoder %s: Decoded data is different from original data", protocol);

		nck_free(&encoder);
		nck_free(&decoder);
	}
}

void test_coded_overflow()
{
	int index, packetno;
	struct nck_encoder encoder;
	struct nck_decoder decoder;
	struct sk_buff skb;
	const char *protocol;
	uint8_t source[1500];
	uint8_t *coded;
	uint8_t *feedback;

	struct nck_option_value options[] = {
		{ "protocol", NULL },
		{ "symbol_size", "1500" },
		{ NULL, NULL }
	};

	for_each_protocol(index) {
		protocol = nck_protocol_name(index);
		if (skip_protocol(protocol)) {
			continue;
		}
		options[0].value = protocol;

		/* TODO fix hang of gack */
		if (strcmp(protocol, "gack") == 0)
			continue;

		/* TODO fix assert of tetrys nck_tetrys_dec_put_coded for uncoded packets */
		if (strcmp(protocol, "tetrys") == 0)
			continue;

		/* TODO fix codarq, it fails when adding the 512th packet */
		if (strcmp(protocol, "codarq") == 0)
			continue;

		TEST_ASSERT_(nck_create_encoder(&encoder, NULL, options, nck_option_from_array) == 0,
				"Encoder %s: Creation failed", protocol);
		TEST_ASSERT_(nck_create_decoder(&decoder, NULL, options, nck_option_from_array) == 0,
				"Decoder %s: Creation failed", protocol);

		TEST_ASSERT_(encoder.source_size == decoder.source_size,
				"Protocol %s: Encoder and decoder have different source_size", protocol);

		TEST_ASSERT_(encoder.coded_size == decoder.coded_size,
				"Protocol %s: Encoder and decoder have different coded_size", protocol);

		TEST_ASSERT_(encoder.feedback_size == decoder.feedback_size,
				"Protocol %s: Encoder and decoder have different feedback_size", protocol);

		TEST_ASSERT_(nck_has_source(&decoder) == 0,
				"Decoder %s: Has a source symbol without receiving anything", protocol);

		/* add packets without retrieving source packets them */
		coded = malloc(encoder.coded_size);
		feedback = malloc(decoder.feedback_size);
		for (packetno = 0; packetno < 10000; ++packetno) {
			skb_new(&skb, source, sizeof(source));
			snprintf((char*)skb_put(&skb, 20), 20, "packet %d", packetno);
			TEST_ASSERT_(nck_put_source(&encoder, &skb) == 0,
					"Encoder %s: Adding a source packet failed", protocol);

			while (nck_has_coded(&encoder) || nck_has_feedback(&decoder)) {
				if (nck_has_coded(&encoder)) {
					skb_new(&skb, coded, encoder.coded_size);
					TEST_ASSERT_(nck_get_coded(&encoder, &skb) == 0,
						    "Encoder %s: Coding failed", protocol);
					TEST_ASSERT_(nck_put_coded(&decoder, &skb) == 0,
						    "Decoder %s: Decoding failed", protocol);
				}

				if (nck_has_feedback(&decoder)) {
					skb_new(&skb, feedback, decoder.feedback_size);
					TEST_ASSERT_(nck_get_feedback(&decoder, &skb) == 0,
						    "Encoder %s: Get feedback failed", protocol);
					TEST_ASSERT_(nck_put_feedback(&encoder, &skb) == 0,
						    "Encoder %s: Put feedback failed", protocol);
				}
			}
		}
		free(coded);
		free(feedback);

		nck_free(&encoder);
		nck_free(&decoder);
	}
}

TEST_LIST = {
	{ "create", test_create },
	{ "decode", test_decode },
	{ "coded_overflow", test_coded_overflow },
	{ NULL }
};
