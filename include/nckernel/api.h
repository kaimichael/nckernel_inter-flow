#ifndef _NCK_API_H_
#define _NCK_API_H_

#include "nckernel.h"

#ifdef  __cplusplus
#include <cassert>
#else
#include <assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NCK_ENCODER_API(prefix)\
	int  prefix ## _enc_set_option(struct prefix ## _enc *encoder, const char *name, const char *value); \
	int  prefix ## _enc_put_source(struct prefix ## _enc *encoder, struct sk_buff *packet); \
	int  prefix ## _enc_get_coded(struct prefix ## _enc *encoder, struct sk_buff *packet); \
	int  prefix ## _enc_has_coded(struct prefix ## _enc *encoder); \
	void prefix ## _enc_flush_coded(struct prefix ## _enc *encoder); \
	int  prefix ## _enc_put_feedback(struct prefix ## _enc *encoder, struct sk_buff *packet); \
	int  prefix ## _enc_full(struct prefix ## _enc *encoder); \
	int  prefix ## _enc_complete(struct prefix ## _enc *encoder); \
	void prefix ## _enc_free(struct prefix ## _enc *encoder); \
	void prefix ## _enc_api(struct nck_encoder *api, struct prefix ## _enc *encoder);

#define NCK_DECODER_API(prefix) \
	int  prefix ## _dec_set_option(struct prefix ## _dec *decoder, const char *name, const char *value); \
	int  prefix ## _dec_put_coded(struct prefix ## _dec *decoder, struct sk_buff *packet); \
	int  prefix ## _dec_get_source(struct prefix ## _dec *decoder, struct sk_buff *packet); \
	int  prefix ## _dec_has_source(struct prefix ## _dec *decoder); \
	void prefix ## _dec_flush_source(struct prefix ## _dec *decoder); \
	int  prefix ## _dec_get_feedback(struct prefix ## _dec *decoder, struct sk_buff *packet); \
	int  prefix ## _dec_has_feedback(struct prefix ## _dec *decoder); \
	int  prefix ## _dec_complete(struct prefix ## _dec *decoder); \
	void prefix ## _dec_free(struct prefix ## _dec *decoder); \
	void prefix ## _dec_api(struct nck_decoder *api, struct prefix ## _dec *decoder);

#define NCK_RECODER_API(prefix) \
	int  prefix ## _rec_set_option(struct prefix ## _rec *recoder, const char *name, const char *value); \
	int  prefix ## _rec_put_coded(struct prefix ## _rec *recoder, struct sk_buff *packet); \
	int  prefix ## _rec_get_coded(struct prefix ## _rec *recoder, struct sk_buff *packet); \
	int  prefix ## _rec_has_coded(struct prefix ## _rec *recoder); \
	void prefix ## _rec_flush_coded(struct prefix ## _rec *recoder); \
	int  prefix ## _rec_get_source(struct prefix ## _rec *recoder, struct sk_buff *packet); \
	int  prefix ## _rec_has_source(struct prefix ## _rec *recoder); \
	void prefix ## _rec_flush_source(struct prefix ## _rec *recoder); \
	int  prefix ## _rec_put_feedback(struct prefix ## _rec *recoder, struct sk_buff *packet); \
	int  prefix ## _rec_get_feedback(struct prefix ## _rec *recoder, struct sk_buff *packet); \
	int  prefix ## _rec_has_feedback(struct prefix ## _rec *recoder); \
	int  prefix ## _rec_complete(struct prefix ## _rec *recoder); \
	void prefix ## _rec_free(struct prefix ## _rec *recoder); \
	void prefix ## _rec_api(struct nck_recoder *api, struct prefix ## _rec *recoder);

#define NCK_ENCODER_IMPL(prefix, _debug, _describe_packet, _get_stats) \
	static int _set_option(void *enc, const char *name, const char *value) \
	{ return prefix ## _enc_set_option((struct prefix ## _enc *)enc, name, value); } \
	static int _put_source(void *enc, struct sk_buff *packet) \
	{ return prefix ## _enc_put_source((struct prefix ## _enc *)enc, packet); } \
	static int _get_coded(void *enc, struct sk_buff *packet) \
	{ return prefix ## _enc_get_coded((struct prefix ## _enc *)enc, packet); } \
	static int _put_feedback(void *enc, struct sk_buff *packet) \
	{ return prefix ## _enc_put_feedback((struct prefix ## _enc *)enc, packet); } \
	static void _flush_coded(void *enc) \
	{ prefix ## _enc_flush_coded((struct prefix ## _enc *)enc); } \
	static int _has_coded(void *enc) \
	{ return prefix ## _enc_has_coded((struct prefix ## _enc *)enc); } \
	static int _full(void *enc) \
	{ return prefix ## _enc_full((struct prefix ## _enc *)enc); } \
	static int _complete(void *enc) \
	{ return prefix ## _enc_complete((struct prefix ## _enc *)enc); } \
	static void _enc_free(void *enc) \
	{ prefix ## _enc_free((struct prefix ## _enc *)enc); } \
	\
	EXPORT void prefix ## _enc_api(struct nck_encoder *api, struct prefix ## _enc *encoder) \
	{ \
		static struct nck_encoder_class type = { \
			_set_option, \
			/*_put_coded*/ NULL, _get_coded, _has_coded, _flush_coded, \
			_put_source, /*_get_source*/ NULL, /*_has_source*/ NULL, /*_flush_source*/ NULL, \
			_put_feedback, /*_get_feedback*/ NULL, /*_has_feedback*/ NULL, \
			_full, \
			_complete, \
			_enc_free, \
			_debug, \
			_describe_packet, \
			_get_stats, \
		};\
		api->type = &type; \
		api->state = encoder; \
		api->source_size = encoder->source_size; \
		api->coded_size = encoder->coded_size; \
		api->feedback_size = encoder->feedback_size; \
		api->_on_source_ready = NULL; \
		api->on_coded_ready = &encoder->on_coded_ready; \
		api->_on_feedback_ready = NULL; \
	}

#define NCK_DECODER_IMPL(prefix, _debug, _describe_packet, _get_stats) \
	static int _set_option(void *dec, const char *name, const char *value) \
	{ return prefix ## _dec_set_option((struct prefix ## _dec *)dec, name, value); } \
	static int _put_coded(void *dec, struct sk_buff *packet) \
	{ return prefix ## _dec_put_coded((struct prefix ## _dec *)dec, packet); } \
	static int _get_source(void *dec, struct sk_buff *packet) \
	{ return prefix ## _dec_get_source((struct prefix ## _dec *)dec, packet); } \
	static int _get_feedback(void *dec, struct sk_buff *packet) \
	{ return prefix ## _dec_get_feedback((struct prefix ## _dec *)dec, packet); } \
	static int _has_feedback(void *dec) \
	{ return prefix ## _dec_has_feedback((struct prefix ## _dec *)dec); } \
	static void _flush_source(void *dec) \
	{ prefix ## _dec_flush_source((struct prefix ## _dec *)dec); } \
	static int _has_source(void *dec) \
	{ return prefix ## _dec_has_source((struct prefix ## _dec *)dec); } \
	static int _complete(void *dec) \
	{ return prefix ## _dec_complete((struct prefix ## _dec *)dec); } \
	static void _dec_free(void *dec) \
	{ prefix ## _dec_free((struct prefix ## _dec *)dec); } \
	\
	EXPORT void prefix ## _dec_api(struct nck_decoder *api, struct prefix ## _dec *decoder) \
	{ \
		static struct nck_decoder_class type = { \
			_set_option, \
			_put_coded, /*_get_coded*/ NULL, /*_has_coded*/ NULL, /*_flush_coded*/ NULL, \
			/*_put_source*/ NULL, _get_source, _has_source, _flush_source, \
			/*_put_feedback*/ NULL, _get_feedback, _has_feedback, \
			/*_full*/ NULL, \
			_complete, \
			_dec_free, \
			_debug, \
			_describe_packet, \
			_get_stats, \
		};\
		api->type = &type; \
		api->state = decoder; \
		api->source_size = decoder->source_size; \
		api->coded_size = decoder->coded_size; \
		api->feedback_size = decoder->feedback_size; \
		api->on_source_ready = &decoder->on_source_ready; \
		api->_on_coded_ready = NULL; \
		api->on_feedback_ready = &decoder->on_feedback_ready; \
	}


#define NCK_RECODER_IMPL(prefix, _debug, _describe_packet, _get_stats) \
	static int _set_option(void *rec, const char *name, const char *value) \
	{ return prefix ## _rec_set_option((struct prefix ## _rec *)rec, name, value); } \
	static int _put_coded(void *rec, struct sk_buff *packet) \
	{ return prefix ## _rec_put_coded((struct prefix ## _rec *)rec, packet); } \
	static int _get_coded(void *rec, struct sk_buff *packet) \
	{ return prefix ## _rec_get_coded((struct prefix ## _rec *)rec, packet); } \
	static int _get_source(void *rec, struct sk_buff *packet) \
	{ return prefix ## _rec_get_source((struct prefix ## _rec *)rec, packet); } \
	static int _get_feedback(void *rec, struct sk_buff *packet) \
	{ return prefix ## _rec_get_feedback((struct prefix ## _rec *)rec, packet); } \
	static int _put_feedback(void *rec, struct sk_buff *packet) \
	{ return prefix ## _rec_put_feedback((struct prefix ## _rec *)rec, packet); } \
	static int _has_feedback(void *rec) \
	{ return prefix ## _rec_has_feedback((struct prefix ## _rec *)rec); } \
	static void _flush_coded(void *rec) \
	{ prefix ## _rec_flush_coded((struct prefix ## _rec *)rec); } \
	static void _flush_source(void *rec) \
	{ prefix ## _rec_flush_source((struct prefix ## _rec *)rec); } \
	static int _has_source(void *rec) \
	{ return prefix ## _rec_has_source((struct prefix ## _rec *)rec); } \
	static int _complete(void *rec) \
	{ return prefix ## _rec_complete((struct prefix ## _rec *)rec); } \
	static int _has_coded(void *rec) \
	{ return prefix ## _rec_has_coded((struct prefix ## _rec *)rec); } \
	static void _rec_free(void *rec) \
	{ prefix ## _rec_free((struct prefix ## _rec *)rec); } \
	\
	EXPORT void prefix ## _rec_api(struct nck_recoder *api, struct prefix ## _rec *recoder) { \
		static struct nck_recoder_class type = { \
			_set_option, \
			_put_coded, _get_coded, _has_coded, _flush_coded, \
			/*_put_source*/ NULL, _get_source, _has_source, _flush_source, \
			_put_feedback, _get_feedback, _has_feedback, \
			/*_full*/ NULL, \
			_complete, \
			_rec_free, \
			_debug, \
			_describe_packet, \
			_get_stats, \
		};\
		api->type = &type; \
		api->state = recoder; \
		api->source_size = recoder->source_size; \
		api->coded_size = recoder->coded_size; \
		api->feedback_size = recoder->feedback_size; \
		api->on_source_ready = &recoder->on_source_ready; \
		api->on_coded_ready = &recoder->on_coded_ready; \
		api->on_feedback_ready = &recoder->on_feedback_ready; \
	}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _API_H_ */
