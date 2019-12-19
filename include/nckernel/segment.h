#ifndef _NCK_SEGMENT_H_
#define _NCK_SEGMENT_H_

#ifdef __cplusplus
#include <cstdint>
#include <cstdlib>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct sk_buff;

struct nck_seg {
    uint8_t *data;
    size_t len;
    size_t space;
};

void nck_seg_new(struct nck_seg *seg, uint8_t *buffer, size_t len, size_t headroom);
int nck_seg_pull(struct nck_seg *seg, struct sk_buff *skb, size_t max_len);

void nck_seg_restore(struct nck_seg *seg, uint8_t *buffer, size_t len);
int nck_seg_push(struct nck_seg *seg, const struct sk_buff *skb);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_SEGMENT_H_ */

