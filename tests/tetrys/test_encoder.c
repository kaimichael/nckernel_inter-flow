#include <assert.h>
#include <stdint.h>

#include <sys/time.h>

#include <nckernel/tetrys.h>
#include <nckernel/skb.h>

int main()
{
	uint8_t buffer[100];
	struct sk_buff input, output;
	struct nck_encoder enc;
	struct nck_tetrys_enc *base_enc;
	uint32_t symbol_size = 20, window_size = 4;
	uint32_t systematic = 4, coded = 1;

	base_enc = nck_tetrys_enc(symbol_size, window_size, NULL, NULL);
	nck_tetrys_enc_set_systematic_phase(base_enc, systematic);
	nck_tetrys_enc_set_coded_phase(base_enc, coded);
	nck_tetrys_enc_api(&enc, base_enc);

	assert(!nck_has_coded(&enc));

	skb_new(&input, (uint8_t*)"test", 5);
	skb_put(&input, 5);

	int source_packets = 10;
	int coded_packets = source_packets * (systematic + coded) / systematic;

	while (source_packets > 0) {
		while(!nck_full(&enc)) {
			printf("put source\n");
			nck_put_source(&enc, &input);
			source_packets -= 1;
		}

		while (nck_has_coded(&enc)) {
			printf("get_coded\n");
			skb_new(&output, buffer, sizeof(buffer));
			nck_get_coded(&enc, &output);
			skb_print(stdout, &output);
			coded_packets -= 1;
		}
	}

	assert(source_packets == 0);
	assert(coded_packets == 0);
	nck_free(&enc);
}

