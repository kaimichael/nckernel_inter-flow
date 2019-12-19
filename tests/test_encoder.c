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
	struct nck_encoder encoder;
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
		TEST_ASSERT_(nck_create_encoder(&encoder, NULL, options, nck_option_from_array) == 0,
				"Encoder %s: Creation failed", protocol);
		nck_free(&encoder);
	}
}

void test_encode()
{
	int index, packetno;
	struct nck_encoder encoder;
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

		TEST_ASSERT_(nck_full(&encoder) == 0,
				"Encoder %s: Full after creation", protocol);

		TEST_ASSERT_(nck_has_coded(&encoder) == 0,
				"Encoder %s: Has coded without a source", protocol);

		TEST_ASSERT_(encoder.coded_size > 0,
				"Encoder %s: Coded size is zero", protocol);

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
		skb_new(&skb, coded, encoder.coded_size);
		TEST_ASSERT_(nck_get_coded(&encoder, &skb) == 0,
				"Encoder %s: Coding failed", protocol);

		free(coded);
		nck_free(&encoder);
	}
}

void test_full()
{
	int index, packetno;
	struct nck_encoder encoder;
	struct sk_buff skb;
	const char *protocol;
	uint8_t source[1500];

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

		TEST_ASSERT_(nck_full(&encoder) == 0,
				"Encoder %s: Full after creation", protocol);

		TEST_ASSERT_(nck_has_coded(&encoder) == 0,
				"Encoder %s: Has coded without a source", protocol);

		TEST_ASSERT_(encoder.coded_size > 0,
				"Encoder %s: Coded size is zero", protocol);

		// add packets until we get some coded packet
		for (packetno = 0; !nck_full(&encoder) && packetno < 10000; ++packetno) {
			skb_new(&skb, source, sizeof(source));
			snprintf((char*)skb_put(&skb, 20), 20, "packet %d", packetno);
			TEST_ASSERT_(nck_put_source(&encoder, &skb) == 0,
					"Encoder %s: Adding a source packet failed", protocol);
		}

		TEST_ASSERT_(nck_full(&encoder) != 0,
				"Encoder %s: Encoder does never signal to be full", protocol);

		nck_free(&encoder);
	}
}

static void increment_on_coded(void *context)
{
	int *counter = (int*)context;
	*counter = *counter + 1;
}

void test_on_coded()
{
	int index, packetno;
	struct nck_encoder encoder;
	struct sk_buff skb;
	const char *protocol;
	int has_coded = 0;
	uint8_t source[1500];

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

		TEST_ASSERT_(nck_full(&encoder) == 0,
				"Encoder %s: Full after creation", protocol);

		TEST_ASSERT_(nck_has_coded(&encoder) == 0,
				"Encoder %s: Has coded without a source", protocol);

		TEST_ASSERT_(encoder.coded_size > 0,
				"Encoder %s: Coded size is zero", protocol);

		nck_trigger_set(encoder.on_coded_ready, &has_coded, increment_on_coded);

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

		TEST_ASSERT_(has_coded > 0,
				"Encoder %s: Trigger on_coded_ready not called", protocol);

		nck_free(&encoder);
	}
}

TEST_LIST = {
	{ "create", test_create },
	{ "encode", test_encode },
	{ "full", test_full },
	{ "on_coded", test_on_coded },
	{ NULL }
};
