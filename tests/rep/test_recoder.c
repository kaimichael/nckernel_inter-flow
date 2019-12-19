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

static void forward(struct nck_rep_enc *enc, struct nck_rep_rec *rec, int count, int drop)
{
	int i;
	uint8_t buffer[symbol_size+100];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_enc_has_coded(enc));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_enc_get_coded(enc, &skb) == 0);

		if (!drop) {
			TEST_ASSERT(nck_rep_rec_put_coded(rec, &skb) == 0);
		}
	}
}

static void reorder(struct nck_rep_enc *enc, struct nck_rep_rec *rec, int count)
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
		TEST_ASSERT(nck_rep_rec_put_coded(rec, &skb) == 0);
	}

	TEST_ASSERT(nck_rep_rec_put_coded(rec, &final) == 0);
}

static void recode(struct nck_rep_rec *rec, int count)
{
	int i;
	uint8_t buffer[symbol_size+100];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_rec_has_coded(rec));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_rec_get_coded(rec, &skb) == 0);
	}
}

static void recv(struct nck_rep_rec *rec, int count)
{
	int i;
	uint8_t buffer[1500];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_rec_has_source(rec));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_rec_get_source(rec, &skb) == 0);
	}
}

void test_recoder()
{
	struct nck_rep_rec *rec;
	struct nck_rep_enc *enc;

	rec = nck_rep_rec(symbols, symbol_size, NULL);
	enc = nck_rep_enc(symbols, symbol_size, NULL);

	// receive 1 and output 1
	send(enc, 1, 0xAA);
	forward(enc, rec, 1, 0);
	recv(rec, 1);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	recode(rec, 1);
	TEST_ASSERT(!nck_rep_rec_has_coded(rec));

	// receive 5 and output 5
	send(enc, 5, 0xBB);
	forward(enc, rec, 5, 0);
	recv(rec, 5);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	recode(rec, 5);
	TEST_ASSERT(!nck_rep_rec_has_coded(rec));

	// force the window to wrap
	send(enc, symbols-1, 0xCC);
	forward(enc, rec, symbols-1, 0);
	recv(rec, symbols-1);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	recode(rec, symbols-1);
	TEST_ASSERT(!nck_rep_rec_has_coded(rec));

	// now test duplicates
	nck_rep_enc_set_redundancy(enc, symbols);
	send(enc, 1, 0xDD);
	forward(enc, rec, 2, 0);
	recv(rec, 1);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	recode(rec, 1);
	TEST_ASSERT(!nck_rep_rec_has_coded(rec));

	// test code rate
	nck_rep_rec_set_redundancy(rec, symbols);
	send(enc, 1, 0xDD);
	forward(enc, rec, 2, 0);
	recv(rec, 1);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	recode(rec, 2);
	TEST_ASSERT(!nck_rep_rec_has_coded(rec));

	// lose one packet
	nck_rep_enc_set_redundancy(enc, 0);
	send(enc, 1, 0xEE);
	forward(enc, rec, 1, 1);
	// deliver many more
	send(enc, symbols-1, 0xFF);
	forward(enc, rec, symbols-1, 0);
	TEST_ASSERT(!nck_rep_rec_has_source(rec));
	// now we deliver 1 more and everything should be released
	send(enc, 1, 0x11);
	forward(enc, rec, 1, 0);
	recv(rec, symbols);

	// finally we test reordering
	send(enc, 2, 0x22);
	reorder(enc, rec, 2);
	recv(rec, 2);

	// reorder some more
	send(enc, symbols, 0x22);
	reorder(enc, rec, symbols);
	recv(rec, symbols);

	// now reordering with some redundancy
	nck_rep_enc_set_redundancy(enc, symbols/2);
	send(enc, 6, 0x22);
	reorder(enc, rec, 9);
	recv(rec, 6);

	nck_rep_rec_free(rec);
	nck_rep_enc_free(enc);
}
