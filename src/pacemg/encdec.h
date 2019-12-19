//
// Created by lsx on 20.07.18.
//

#ifndef NCKERNEL_ENCDEC_H
#define NCKERNEL_ENCDEC_H

//debugging outputs

//#define ENC_ADD_DEL_CONTAINER	//shows adding and deleting of containers in the encoder
//#define ENC_PACKETS_CODED		//shows coded packets sent        ("enc", generation, no in generation, encoder's rank, data, no of sent packets)
//#define DEC_PACKETS_CODED		//shows coded packets received    ("dec", generation, no in generation, encoder's rank, data)
//#define ENC_PACKETS_SOURCE  	//shows source packets sent       ("enc", generation, index, data)
//#define DEC_PACKETS_SOURCE  	//shows source packets received   ("dec", generation, index, data)
//#define DEC_PACKETS_FEEDBACK	//shows feedback packets sent     ("dec", generation, no in generation, decoder's rank)
//#define ENC_PACKETS_FEEDBACK	//shows feedback packets received ("enc", generation, no in generation, decoder's rank, "ack"/"container deleted"/"not computed")
//#define ENC_CONTAINER_DELETED	//shows deleted containers in the encoder
//#define DEC_HAS_SOURCE  		//show output and generationg line of nck_pacemg_dec_has_source
//#define DEC_HAS_FEEDBACK		//shows calls of nck_pacemg_dec_has_feedback
//#define DEC_STATS				//shows new value of gen_oldest in nck_pacemg_update_decoder_stat
//#define DEC_RANK				//shows decoders target and actual rank in nck_pacemg_dec_put_coded

#define MAX_MAX_ACTIVE_CONTAINERS 100	//maximum number for max_active_containers due to simulator restraints

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

struct nck_pacemg_pkt_header{
	uint32_t generation;	//generation no
	uint16_t rank;			//encoder's rank
	uint16_t seqno;			//no of packet in generation (starting at 0)
	uint32_t global_seqno;			//no of packet in generation (starting at 0)
	uint8_t  feedback_flag;	//1 if feedback should be generated
};
static const size_t nck_pacemg_pkt_header_size = 4 + 2 + 2 + 4 + 1;

struct nck_pacemg_pkt_feedback{
	uint32_t generation;	//generation no
	uint16_t rank_enc;		//encoder's rank
	uint16_t rank_dec;		//decoder's rank
    uint16_t seqno;			//packet's no in generation (starting at 0)
};
static const size_t nck_pacemg_pkt_feedback_size_single = 4 + 2 + 2 + 2;
static const size_t nck_pacemg_pkt_feedback_additional = 4 + 4;

#endif //NCKERNEL_ENCDEC_H
