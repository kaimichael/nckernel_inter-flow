#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>

#include <nckernel/tetrys.h>
#include <nckernel/skb.h>

int main()
{
	uint8_t buffer[100];
	int length;
	uint32_t symbols = 16, symbol_size = 20, window_size = 8;
	struct sk_buff input, output;

	struct nck_encoder enc;
	struct nck_tetrys_enc *base_enc;

	struct nck_decoder dec;
	struct nck_tetrys_dec *base_dec;

	srand(0);

	base_enc = nck_tetrys_enc(symbol_size, window_size, NULL, NULL);
	nck_tetrys_enc_set_systematic_phase(base_enc, 2);
	nck_tetrys_enc_set_coded_phase(base_enc, 1);
	nck_tetrys_enc_api(&enc, base_enc);

	base_dec = nck_tetrys_dec(symbols, window_size);
	nck_tetrys_dec_api(&dec, base_dec);

	assert(!nck_has_source(&dec));
	assert(!nck_has_coded(&enc));

	int sent_packets = 0;
	int receive_packets = 30;

	while (receive_packets > 0) {
		while(!nck_full(&enc)) {
			memset(buffer, 0, sizeof(buffer));
			length = sprintf((char*)buffer, "packet %d", sent_packets);
			skb_new(&input, buffer, sizeof(buffer));
			skb_put(&input, length+1);
			printf("\nput source\n");
			nck_put_source(&enc, &input);
			sent_packets += 1;
		}

		assert(nck_has_coded(&enc));

		while (nck_has_coded(&enc)) {
			skb_new(&output, buffer, sizeof(buffer));

			printf("\nget coded\n");
			nck_get_coded(&enc, &output);
			skb_print(stdout, &output);

			printf("\nput coded\n");
			if (rand()%3) {
				nck_put_coded(&dec, &output);
			} else {
				printf("lost\n");
			}

			while (nck_has_source(&dec)) {
				memset(buffer, 0, sizeof(buffer));
				skb_new(&output, buffer, sizeof(buffer));
				nck_get_source(&dec, &output);

				receive_packets -= 1;
				printf("\ndecoder: %s\n", output.data);
			}
		}
	}

	nck_free(&enc);
	nck_free(&dec);
}

