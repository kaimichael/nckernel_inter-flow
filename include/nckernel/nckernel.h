/** @file
 *
 * Main header file containing all functions required to use the encoders and
 * decoders from libnckernel.
 */
#ifndef _NCKERNEL_H_
#define _NCKERNEL_H_

#ifdef __cplusplus
#include <cstdint>
#include <cstdio>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nck_coder_type {
	NCK_ENCODER,
	NCK_DECODER,
	NCK_RECODER
};

struct nck_coder;
struct nck_encoder;
struct nck_decoder;
struct nck_recoder;
struct nck_trigger;
struct sk_buff;
struct nck_timer;

typedef const char *(*nck_opt_getter)(void *context, const char *option);

struct nck_option_value {
	const char *name;
	const char *value;
};

enum nck_stat_type {
	NCK_STATS_GET_SOURCE,
	NCK_STATS_PUT_SOURCE,
	NCK_STATS_GET_CODED,
	NCK_STATS_PUT_CODED,
	NCK_STATS_GET_FEEDBACK,
	NCK_STATS_PUT_FEEDBACK,
	NCK_STATS_PUT_CODED_INNOVATIVE,
	NCK_STATS_PUT_CODED_REDUNDANT,
	NCK_STATS_PUT_CODED_CONFLICT,
	NCK_STATS_PUT_CODED_OUTDATED,
	NCK_STATS_GET_CODED_SYSTEMATIC,
	NCK_STATS_GET_CODED_REPAIR,
	NCK_STATS_GET_CODED_RETRY,
	NCK_STATS_GET_CODED_TX_ATTEMPTS_ZERO,
	NCK_STATS_GET_CODED_DISCARDED_ZERO_ONLY,
	NCK_STATS_TIMER_FLUSH,
	NCK_STATS_TIMER_FB_FLUSH,
	NCK_STATS_UNDECODED_SYMBOLS,
	NCK_STATS_MAX
};

static __inline__ const char *nck_stat_string(enum nck_stat_type nck_stat_type)
{
	switch (nck_stat_type) {
	case NCK_STATS_GET_SOURCE: return "GET_SOURCE";
	case NCK_STATS_PUT_SOURCE: return "PUT_SOURCE";
	case NCK_STATS_GET_CODED: return "GET_CODED";
	case NCK_STATS_PUT_CODED: return "PUT_CODED";
	case NCK_STATS_GET_FEEDBACK: return "GET_FEEDBACK";
	case NCK_STATS_PUT_FEEDBACK: return "PUT_FEEDBACK";
	case NCK_STATS_PUT_CODED_INNOVATIVE: return "PUT_CODED_INNOVATIVE";
	case NCK_STATS_PUT_CODED_REDUNDANT: return "PUT_CODED_REDUNDANT";
	case NCK_STATS_PUT_CODED_CONFLICT: return "PUT_CODED_CONFLICT";
	case NCK_STATS_PUT_CODED_OUTDATED: return "PUT_CODED_OUTDATED";
	case NCK_STATS_GET_CODED_SYSTEMATIC: return "GET_CODED_SYSTEMATIC";
	case NCK_STATS_GET_CODED_REPAIR: return "GET_CODED_REPAIR";
	case NCK_STATS_GET_CODED_RETRY: return "GET_CODED_RETRY";
	case NCK_STATS_GET_CODED_TX_ATTEMPTS_ZERO: return "GET_CODED_TX_ATTEMPTS_ZERO";
	case NCK_STATS_GET_CODED_DISCARDED_ZERO_ONLY: return "GET_CODED_DISCARDED_ZERO_ONLY";
	case NCK_STATS_TIMER_FLUSH: return "TIMER_FLUSH";
	case NCK_STATS_TIMER_FB_FLUSH: return "TIMER_FB_FLUSH";
	case NCK_STATS_UNDECODED_SYMBOLS: return "UNDECODED_SYMBOLS";
	case NCK_STATS_MAX: break;
	}

	return "unknown";
}

struct nck_stats {
	uint64_t s[NCK_STATS_MAX];
};

/**
 * nck_option_from_array - Retrieve an option value from an array of struct nck_option_value.
 * @value_array: Pointer to a NULL-terminated array of struct nck_option_value.
 * @name: Name of the option to lookup.
 * @return: Value of the option or NULL if it is not found.
 */
const char *nck_option_from_array(void *value_array, const char *name);

/**
 * nck_protocol_find - Find the index of a given protocol name in the protocol list.
 * @name: Name of the protocol
 * @return: Index of the protocol or -1 if it cannot be found
 */
int nck_protocol_find(const char *name);
/**
 * nck_protocol_name - Gets the name of the protocol with the specified index.
 * @index: Lookup index in the protocol list
 * @return: Name of the protocol or NULL at the end of the protocol list
 */
const char *nck_protocol_name(int index);
/**
 * nck_protocol_description - Gets a description for the protocol at the given index
 * @index: Lookup index in the protocol list
 * @return: Description of the protocol or NULL at the end of the protocol list
 */
const char *nck_protocol_description(int index);

/**
 * nck_create_coder - Configures a generic coder structure as a specific encoder, decoder or recoder implementation.
 * @coder: Coder structure to configure
 * @type: Specify the coder type
 * @timer: Timer implementation that will be used by the coder
 * @context: Contextual object that will be passed to get_opt
 * @get_opt: Function used to get configuration values for the coder
 * @return: Returns 0 on success
 */
int nck_create_coder(struct nck_coder *coder, enum nck_coder_type type, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_create_encoder - Configures a generic encoder structure as a specific encoder implementation.
 * @encoder: Encoder structure to configure
 * @timer: Timer implementation that will be used by the coder
 * @context: Contextual object that will be passed to get_opt
 * @get_opt: Function used to get configuration values for the coder
 * @return: Returns 0 on success
 */
int nck_create_encoder(struct nck_encoder *encoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_create_decoder - Configures a generic decoder structure as a specific decoder implementation.
 * @decoder: decoder structure to configure
 * @timer: Timer implementation that will be used by the coder
 * @context: Contextual object that will be passed to get_opt
 * @get_opt: Function used to get configuration values for the coder
 * @return: Returns 0 on success
 */
int nck_create_decoder(struct nck_decoder *decoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);
/**
 * nck_create_recoder - Configures a generic recoder structure as a specific recoder implementation.
 * @recoder: recoder structure to configure
 * @timer: Timer implementation that will be used by the coder
 * @context: Contextual object that will be passed to get_opt
 * @get_opt: Function used to get configuration values for the coder
 * @return: Returns 0 on success
 */
int nck_create_recoder(struct nck_recoder *recoder, struct nck_timer *timer, void *context, nck_opt_getter get_opt);

/**
 * nck_free - Free all resources used by a coder.
 * @c: Pointer to the coder structure
 */
#define nck_free(c) (c)->type->free((c)->state)

/*
 * nck_set_option - Set an option for a coder.
 * @c: Pointer to the coder structure
 * @name: Name of the option to set
 * @value: Value for the option
 */
#define nck_set_option(c, name, value) (c)->type->set_option((c)->state, (name), (value))

/**
 * nck_put_source - Read a source symbol into the coder.
 * @c: Pointer to the coder structure
 * @packet: sk_buff that contains the source symbol
 */
#define nck_put_source(c, packet) (c)->type->put_source((c)->state, (packet))
/**
 * nck_get_source - Retrieve a decoded source symbol.
 * @c: Pointer to the coder structure
 * @packet: A packet where the decoded symbol will be stored
 * @return: Returns 0 on success
 */
#define nck_get_source(c, packet) (c)->type->get_source((c)->state, (packet))
/**
 * nck_has_source - Check if a source symbol can be extracted.
 * @c: Pointer to the coder structure
 * @return: Returns 0 if no source symbol is available
 */
#define nck_has_source(c) (c)->type->has_source((c)->state)
/**
 * nck_flush_source - Make additional source symbols available if possible.
 * @c: Pointer to the coder structure
 *
 * This flush indicates that no more coded symbols are expected. A decoder should react by
 * making all decoded source symbols available, even if that means skipping over some
 * undecoded packets. These skipped symbols can never be recovered again.
 */
#define nck_flush_source(c) (c)->type->flush_source((c)->state)
/**
 * nck_on_source_ready - Register a function that will be called when source symbols become available.
 * @c: Pointer to the coder structure
 * @context: Contextual object that will be passed to the callback
 * @callback: Function that will be called when a source symbol is available
 */
#define nck_on_source_ready(c, context, callback)\
	nck_trigger_set((c)->on_source_ready, context, callback)

/**
 * nck_put_coded - Read a coded symbol into the coder.
 * @c: Pointer to the coder structure
 * @packet: A packet containing the coded data
 * @return: Returns 0 on success
 */
#define nck_put_coded(c, packet) (c)->type->put_coded((c)->state, (packet))
/**
 * nck_get_coded - Retrieve a decoded coded symbol.
 * @c: Pointer to the coder structure
 * @packet: A packet where the coded symbol will be stored
 * @return: Returns 0 on success
 */
#define nck_get_coded(c, packet) (c)->type->get_coded((c)->state, (packet))
/**
 * nck_has_codedCheck if a coded symbol can be extracted.
 * @c: Pointer to the coder structure
 * @return: Returns 0 if no coded symbol is available
 */
#define nck_has_coded(c) (c)->type->has_coded((c)->state)
/**
 * nck_flush_coded - Make additional coded symbols available if possible.
 * @c: Pointer to the coder structure
 *
 * This flush indicates that no new source symbols are expected to arrive, but
 * more coded symbols are requested. If the encoder was waiting for more data then
 * this is the signal to wrap it up and generate the coded symbols.
 */
#define nck_flush_coded(c) (c)->type->flush_coded((c)->state)
/**
 * nck_on_coded_ready - Register a function that will be called when coded symbols become available.
 * @c: Pointer to the coder structure
 * @context: Contextual object that will be passed to the callback
 * @callback: Function that will be called when a coded symbol is available
 */
#define nck_on_coded_ready(c, context, callback)\
	nck_trigger_set((c)->on_coded_ready, context, callback)

/**
 * nck_put_feedback - Parse feedback from another coder.
 * @c: Pointer to the coder structure
 * @packet: A packet containing the feedback data
 * @return: Returns 0 on success
 */
#define nck_put_feedback(c, packet) (c)->type->put_feedback((c)->state, (packet))
/**
 * nck_get_feedback - Retrieve a feedback packet.
 * @c: Pointer to the coder structure
 * @packet: A packet where the feedback will be stored
 * @return: Returns 0 on success
 */
#define nck_get_feedback(c, packet) (c)->type->get_feedback((c)->state, (packet))
/**
 * nck_has_feedback - Check if feedback symbol can be extracted.
 * @c: Pointer to the coder structure
 * @return: Returns 0 if no feedback symbol is available
 */
#define nck_has_feedback(c) (c)->type->has_feedback((c)->state)
/**
 * nck_on_feedback_ready - Register a function that will be called when feedback becomes available.
 * @c: Pointer to the coder structure
 * @context: Contextual object that will be passed to the callback
 * @callback: Function that will be called when feedback is available
 */
#define nck_on_feedback_ready(c, context, callback)\
	nck_trigger_set((c)->on_feedback_ready, context, callback)

#define nck_complete(c) (c)->type->complete((c)->state)
/**
 * nck_complete - Check if source symbols can be added to the coder.
 *
 * @c: Pointer to the coder structure
 */
#define nck_full(c) (c)->type->full((c)->state)
/**
 * nck_full - Returns a string with debug information for a coder
 *
 * @c: Pointer to the coder structure
 * @return: Returns a description string
 */
#define nck_debug(c) ((c)->type->debug ? (c)->type->debug((c)->state) : "\"error\":\"not implemented\"")
/**
 * nck_debug - Returns a string with debug information for a packet generated by a coder
 *
 * @c: Pointer to the coder structure
 * @packet: Packet payload to describe
 * @return: Returns a description string
 */
#define nck_describe_packet(c, packet) ((c)->type->describe_packet ? (c)->type->describe_packet((c)->state, (packet)) : "\"error\":\"not implemented\"")
/**
 * nck_get_stats - Returns a pointer to the a struct nck_stats if available, NULL otherwise
 *
 * @c: Pointer to the coder structure
 * @return: Returns a description string
 */
#define nck_get_stats(c) ((c)->type->get_stats ? (c)->type->get_stats((c)->state) : NULL)

/* Trigger functions */
void nck_trigger_init(struct nck_trigger *trigger);
void nck_trigger_set(struct nck_trigger *trigger, void *context, void (*callback)(void *context));
void nck_trigger_call(struct nck_trigger *trigger);

/* structures */

struct nck_trigger {
	void *context;
	void (*callback)(void *context);
};

/**
 * NCK_CODER_TYPE_MEMBERS - Helper macro to define common scoder structures.
 * @E: Marks all functions not available to encoders.
 * @D: Marks all functions not available to decoders.
 * @R: Marks all functions not available to recoders.
 *
 * This macro lists all member functions for all different coder types. The
 * parameters E, D and R are used to prefix functions that should not be
 * available to encoders, decoders and recoders, respectively. The parameters
 * are set to _ to prefix these invalid functions. This way the different coder
 * types are structurally identical but calling an invalid function triggers a
 * compiler error.
 *
 */
#define NCK_CODER_TYPE_MEMBERS(E,D,R) \
	int   (*        set_option     )(void *coder, const char *name, const char *value); \
	int   (* E##    put_coded      )(void *coder, struct sk_buff *packet); \
	int   (* D##    get_coded      )(void *coder, struct sk_buff *packet); \
	int   (* D##    has_coded      )(void *coder); \
	void  (* D##    flush_coded    )(void *coder); \
	int   (* D##R## put_source     )(void *coder, struct sk_buff *packet); \
	int   (* E##    get_source     )(void *coder, struct sk_buff *packet); \
	int   (* E##    has_source     )(void *coder); \
	void  (* E##    flush_source   )(void *coder); \
	int   (* D##    put_feedback   )(void *coder, struct sk_buff *packet); \
	int   (* E##    get_feedback   )(void *coder, struct sk_buff *packet); \
	int   (* E##    has_feedback   )(void *coder); \
	int   (* D##R## full           )(void *coder); \
	int   (*        complete       )(void *coder); \
	void  (*        free           )(void *coder); \
	char* (*        debug          )(void *coder); \
	char* (*        describe_packet)(void *coder, struct sk_buff *packet); \
	struct nck_stats *(*get_stats  )(void *coder);

#define NCK_CODER_MEMBERS(E,D,R) \
	void *	state; \
	size_t source_size; \
	size_t coded_size; \
	size_t feedback_size; \
	struct nck_trigger * E##    on_source_ready; \
	struct nck_trigger * E##    on_feedback_ready; \
	struct nck_trigger *    D## on_coded_ready;

struct nck_coder_class {
	NCK_CODER_TYPE_MEMBERS(,,)
};

struct nck_encoder_class {
	NCK_CODER_TYPE_MEMBERS(_,,)
};

struct nck_decoder_class {
	NCK_CODER_TYPE_MEMBERS(,_,)
};

struct nck_recoder_class {
	NCK_CODER_TYPE_MEMBERS(,,_)
};

struct nck_coder {
	const struct nck_coder_class *type;
	NCK_CODER_MEMBERS(,,)
};

struct nck_encoder {
	const struct nck_encoder_class *type;
	NCK_CODER_MEMBERS(_,,)
};

struct nck_decoder {
	const struct nck_decoder_class *type;
	NCK_CODER_MEMBERS(,_,)
};

struct nck_recoder {
	const struct nck_recoder_class *type;
	NCK_CODER_MEMBERS(,,_)
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCKERNEL_H_ */
