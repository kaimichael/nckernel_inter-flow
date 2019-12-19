#define TEST_NO_MAIN
#include <cutest.h>
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>

#include <nckernel/rep.h>
#include <nckernel/skb.h>

#define TEST_ASSERT(cond) assert(TEST_CHECK(cond))
#define TEST_ASSERT_(cond, ...) assert(TEST_CHECK_(cond, __VA_ARGS__))

static uint32_t symbols = 32, symbol_size = 1400;

static void send(struct nck_rep_enc *enc, int count, uint8_t payload)
{
	int i;
	uint8_t buffer[symbol_size];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		skb_new(&skb, buffer, sizeof(buffer));
		skb_put(&skb, sizeof(buffer));
		memset(skb.data, (uint8_t)(payload+i), skb.len);
		TEST_ASSERT(nck_rep_enc_put_source(enc, &skb) == 0);
	}
}

static void forward(struct nck_rep_enc *enc, struct nck_rep_dec *dec, int count, int drop)
{
	int i;
	uint8_t buffer[symbol_size+100];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_enc_has_coded(enc));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_enc_get_coded(enc, &skb) == 0);

		if (!drop) {
			TEST_ASSERT(nck_rep_dec_put_coded(dec, &skb) == 0);
		}
	}
}

static void forward_feedback(struct nck_rep_enc *enc, struct nck_rep_dec *dec)
{
	uint8_t buffer[symbols+100];
	struct sk_buff skb;

	TEST_ASSERT(nck_rep_dec_has_feedback(dec));
	skb_new(&skb, buffer, sizeof(buffer));
	TEST_ASSERT(nck_rep_dec_get_feedback(dec, &skb) == 0);
	TEST_ASSERT(nck_rep_enc_put_feedback(enc, &skb) == 0);
}

static void reorder(struct nck_rep_enc *enc, struct nck_rep_dec *dec, int count)
{
	int i;
	uint8_t fbuf[symbol_size+100];
	uint8_t buffer[symbol_size+100];
	struct sk_buff skb, final;

	TEST_ASSERT(nck_rep_enc_has_coded(enc));
	skb_new(&final, fbuf, sizeof(fbuf));
	TEST_ASSERT(nck_rep_enc_get_coded(enc, &final) == 0);

	for (i = 0; i < count-1; ++i) {
		TEST_ASSERT(nck_rep_enc_has_coded(enc));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_enc_get_coded(enc, &skb) == 0);
		TEST_ASSERT(nck_rep_dec_put_coded(dec, &skb) == 0);
	}

	TEST_ASSERT(nck_rep_dec_put_coded(dec, &final) == 0);
}

static void recv(struct nck_rep_dec *dec, int count)
{
	int i;
	uint8_t buffer[1500];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_dec_has_source(dec));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_dec_get_source(dec, &skb) == 0);
	}
}

void test_decoder()
{
	struct nck_rep_dec *dec;
	struct nck_rep_enc *enc;

	dec = nck_rep_dec(symbols, symbol_size, NULL);
	enc = nck_rep_enc(symbols, symbol_size, NULL);

	// receive 1 and output 1
	send(enc, 1, 0xAA);
	forward(enc, dec, 1, 0);
	recv(dec, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	// receive 5 and output 5
	send(enc, 5, 0xBB);
	forward(enc, dec, 5, 0);
	recv(dec, 5);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	// force the window to wrap
	send(enc, symbols-1, 0xCC);
	forward(enc, dec, symbols-1, 0);
	recv(dec, symbols-1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	// now test duplicates
	nck_rep_enc_set_redundancy(enc, symbols);
	send(enc, 1, 0xDD);
	forward(enc, dec, 2, 0);
	recv(dec, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	// lose one packet
	nck_rep_enc_set_redundancy(enc, 0);
	send(enc, 1, 0xEE);
	forward(enc, dec, 1, 1);
	// deliver many more
	send(enc, symbols-1, 0xFF);
	forward(enc, dec, symbols-1, 0);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	// now we deliver 1 more and everything should be released
	send(enc, 1, 0x11);
	forward(enc, dec, 1, 0);
	recv(dec, symbols);

	// finally we test reordering
	send(enc, 2, 0x22);
	reorder(enc, dec, 2);
	recv(dec, 2);

	// reorder some more
	send(enc, symbols, 0x22);
	reorder(enc, dec, symbols);
	recv(dec, symbols);

	// now reordering with some redundancy
	nck_rep_enc_set_redundancy(enc, symbols/2);
	send(enc, 6, 0x22);
	reorder(enc, dec, 9);
	recv(dec, 6);

	nck_rep_dec_free(dec);
	nck_rep_enc_free(enc);
}

void test_feedback()
{
	struct nck_rep_dec *dec;
	struct nck_rep_enc *enc;

	dec = nck_rep_dec(symbols, symbol_size, NULL);
	enc = nck_rep_enc(symbols, symbol_size, NULL);

	// receive 1 and output 1
	send(enc, 1, 0xAA);
	forward(enc, dec, 1, 0);
	recv(dec, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// drop 1
	send(enc, 1, 0xBB);
	forward(enc, dec, 1, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// receive 1
	send(enc, 1, 0xCC);
	forward(enc, dec, 1, 0);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// forward feedback from decoder to encoder
	forward_feedback(enc, dec);
	TEST_ASSERT(nck_rep_enc_has_coded(enc));

	// now encoder should retransmit the missing packet
	forward(enc, dec, 1, 0);
	recv(dec, 2);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	nck_rep_dec_free(dec);
	nck_rep_enc_free(enc);
}

void test_decoder_flush()
{
	struct nck_rep_dec *dec;
	struct nck_rep_enc *enc;

	dec = nck_rep_dec(symbols, symbol_size, NULL);
	enc = nck_rep_enc(symbols, symbol_size, NULL);

	// receive 1
	send(enc, 1, 0xAA);
	forward(enc, dec, 1, 0);
	recv(dec, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// drop 1
	send(enc, 1, 0xBB);
	forward(enc, dec, 1, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// receive 1 more
	send(enc, 1, 0xCC);
	forward(enc, dec, 1, 0);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// now one packet is stuck, we need to flush it
	nck_rep_dec_flush_source(dec);
	recv(dec, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	// drop 1
	send(enc, 1, 0xDD);
	forward(enc, dec, 1, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// receive 1
	send(enc, 1, 0xEE);
	forward(enc, dec, 1, 0);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// drop 1
	send(enc, 1, 0xFF);
	forward(enc, dec, 1, 1);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// receive 1
	send(enc, 1, 0x11);
	forward(enc, dec, 1, 0);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));
	TEST_ASSERT(!nck_rep_enc_has_coded(enc));

	// flush both two stuck packets
	nck_rep_dec_flush_source(dec);
	recv(dec, 2);
	TEST_ASSERT(!nck_rep_dec_has_source(dec));

	nck_rep_dec_free(dec);
	nck_rep_enc_free(enc);
}
