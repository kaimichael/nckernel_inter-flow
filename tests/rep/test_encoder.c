#define TEST_NO_MAIN
#include <cutest.h>
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <sys/time.h>

#include <nckernel/rep.h>
#include <nckernel/skb.h>

#define TEST_ASSERT(cond) assert(TEST_CHECK(cond))
#define TEST_ASSERT_(cond, ...) assert(TEST_CHECK_(cond, __VA_ARGS__))

static void send(struct nck_rep_enc *enc, int count, uint8_t payload, size_t len)
{
	int i;
	uint8_t buffer[len];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		skb_new(&skb, buffer, sizeof(buffer));
		skb_put(&skb, len);
		memset(skb.data, (uint8_t)(payload+i), skb.len);
		TEST_ASSERT(nck_rep_enc_put_source(enc, &skb) == 0);
	}
}

static void recv(struct nck_rep_enc *enc, int count, size_t len)
{
	int i;
	uint8_t buffer[len];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_enc_has_coded(enc));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_enc_get_coded(enc, &skb) == 0);
	}
}

void test_encoder()
{
	uint32_t symbols = 32, symbol_size = 1400;
	struct nck_rep_enc *enc;

	enc = nck_rep_enc(symbols, symbol_size, NULL);
	//nck_rep_enc_set_redundancy(enc, 0);

	// put and get 1 packet
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 1, 0xAA, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 1, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// put and get 1 packet
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 1, 0xBB, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 1, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// put and get 5 packet
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 5, 0xCC, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 5, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// send duplicate packets
	nck_rep_enc_set_redundancy(enc, symbols);

	// send 1 and receive 2
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 1, 0xDD, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 2, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// send 5 and receive 10
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 5, 0xEE, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 10, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// repeat every second packet
	nck_rep_enc_set_redundancy(enc, symbols/2);

	// send 2 and receive 3
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 2, 0xFF, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 3, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// send 6 and receive 9
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));
	send(enc, 6, 0x11, symbol_size);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	recv(enc, 9, 1500);
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	nck_rep_enc_free(enc);
}
