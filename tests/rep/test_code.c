#include <cutest.h>
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nckernel/rep.h>
#include <nckernel/skb.h>

#define TEST_ASSERT(cond) assert(TEST_CHECK(cond))
#define TEST_ASSERT_(cond, ...) assert(TEST_CHECK_(cond, __VA_ARGS__))

static uint32_t symbols = 10, symbol_size = 1400;

static void get_source(struct nck_rep_dec *dec, int count, uint8_t payload)
{
	int i;
	uint8_t buffer[symbol_size];
	struct sk_buff skb;

	for (i = 0; i < count; ++i) {
		TEST_ASSERT(nck_rep_dec_has_source(dec));
		skb_new(&skb, buffer, sizeof(buffer));
		TEST_ASSERT(nck_rep_dec_get_source(dec, &skb) == 0);
		TEST_ASSERT_(skb.data[0] == payload + i,
				"Check data, expected %02x, actual %02x", payload+i, skb.data[0]);
	}
}

static void put_source(struct nck_rep_enc *enc, int count, uint8_t payload)
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

static void get_coded(struct nck_rep_enc *enc, struct sk_buff *skb)
{
	size_t size = symbol_size+100;

	TEST_ASSERT(nck_rep_enc_has_coded(enc));
	skb_new(skb, malloc(size), size);
	TEST_ASSERT(nck_rep_enc_get_coded(enc, skb) == 0);
}

static void put_coded(struct nck_rep_dec *dec, struct sk_buff *skb)
{
	TEST_ASSERT(nck_rep_dec_put_coded(dec, skb) == 0);
}

static void get_feedback(struct nck_rep_dec *dec, struct sk_buff *skb)
{
	size_t size = symbols+100;
	TEST_ASSERT(nck_rep_dec_has_feedback(dec));
	skb_new(skb, malloc(size), size);
	TEST_ASSERT(nck_rep_dec_get_feedback(dec, skb) == 0);
}

static void put_feedback(struct nck_rep_enc *enc, struct sk_buff *skb)
{
	TEST_ASSERT(nck_rep_enc_put_feedback(enc, skb) == 0);
}

static void run_scenario(struct nck_rep_enc *enc, struct nck_rep_dec *dec, unsigned delay, unsigned packets, unsigned transmissions, int *coded_loss)
{
	unsigned i, enc_no = 0, dec_no = 1; // we start with dec_no 1 because we will lose the first packet
	struct sk_buff coded[delay];
	struct sk_buff feedback[delay];

	for (i = 0; i < delay; ++i) {
		coded[i].head = NULL;
		feedback[i].head = NULL;
	}

	// we can fill up the encoder
	put_source(enc, symbols, 0xAA);
	enc_no += symbols;
	TEST_ASSERT(nck_rep_enc_full(enc));

	for (i = 0; i < transmissions; ++i) {
		while (!nck_rep_enc_full(enc) && enc_no < packets) {
			put_source(enc, 1, 0xAA + enc_no);
			enc_no += 1;
		}

		// deliver feedback before we might overwrite it
		if (i >= delay && feedback[i%delay].head != NULL) {
			// always deliver feedback
			put_feedback(enc, &feedback[i%delay]);

			// free feedback
			free(feedback[i%delay].head);
			feedback[i%delay].head = NULL;
		}

		// deliver coded packets before we might overwrite it
		if (i >= delay && coded[i%delay].head != NULL) {
			// only deliver if it is not lost
			if (!coded_loss[i-delay]) {
				put_coded(dec, &coded[i%delay]);
				get_feedback(dec, &feedback[i%delay]);
			}

			// free coded data
			free(coded[i%delay].head);
			coded[i%delay].head = NULL;
		}

		while (nck_rep_dec_has_source(dec)) {
			TEST_ASSERT_(dec_no < enc_no, "Decoder follows encoder: %u < %u", dec_no, enc_no);
			get_source(dec, 1, 0xAA + dec_no);
			dec_no += 1;
		}

		if (nck_rep_enc_has_coded(enc)) {
			get_coded(enc, &coded[i%delay]);
		}
	}

	TEST_ASSERT_(dec_no == packets, "Decode all data: %u of %u", dec_no, packets);

	for (i = 0; i < delay; ++i) {
		if (coded[i].head != NULL) {
			free(coded[i].head);
		}

		if (feedback[i].head != NULL) {
			free(feedback[i].head);
		}
	}
}

void test_arq()
{
	struct nck_rep_dec *dec;
	struct nck_rep_enc *enc;
	int coded_loss[] = {
		1, 0, 1, 0, 0, 0, 1, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	dec = nck_rep_dec(symbols, symbol_size, NULL);
	enc = nck_rep_enc(symbols, symbol_size, NULL);

	nck_rep_enc_set_redundancy(enc, 0);

	run_scenario(enc, dec, 4, 20, sizeof(coded_loss)/sizeof(*coded_loss), coded_loss);

	nck_rep_dec_free(dec);
	nck_rep_enc_free(enc);

}

void test_encoder();
void test_decoder();
void test_decoder();
void test_decoder_flush();
void test_feedback();
void test_recoder();

TEST_LIST = {
	{ "encoder", test_encoder },
	{ "decoder", test_decoder },
	{ "decoder_flush", test_decoder_flush },
	{ "feedback", test_feedback },
	{ "recoder", test_recoder },
	{ "arq", test_arq },
	{ 0 }
};
