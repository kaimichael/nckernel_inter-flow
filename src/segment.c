#include <assert.h>
#include <errno.h>
#include <string.h>

#include <nckernel/segment.h>
#include <nckernel/skb.h>

void nck_seg_new(struct nck_seg *seg, uint8_t *buffer, size_t len, size_t headroom)
{
	seg->data = buffer + headroom;
	seg->len = len - headroom;
	seg->space = headroom;
}

int nck_seg_pull(struct nck_seg *seg, struct sk_buff *skb, size_t max_len)
{
	size_t len;

	if (seg->len == 0) {
		return 1;
	}

	skb->head = seg->data - seg->space;
	skb->data = seg->data;

	len = max_len - seg->space;
	
	if (seg->len < len) {
		skb->len = seg->len;
		seg->data += seg->len;
		seg->len = 0;
	} else {
		skb->len = len;
		seg->data += len;
		seg->len -= len;
	}

	return 0;
}

void nck_seg_restore(struct nck_seg *seg, uint8_t *buffer, size_t len)
{
	seg->data = buffer;
	seg->len = 0;
	seg->space = len;
}

int nck_seg_push(struct nck_seg *seg, const struct sk_buff *skb)
{
	assert(skb->len <= seg->space);

	memcpy(seg->data, skb->data, skb->len);
	seg->data += skb->len;
	seg->len += skb->len;
	seg->space -= skb->len;

	return 0;
}

