/* Linux-like socket buffer and helper functions
 *
 * An introduction is given in the following resources:
 *   http://www.skbuff.net/skbbasic.html
 *   http://vger.kernel.org/~davem/skb_data.html
 */

#ifndef _NCK_SKB_H_
#define _NCK_SKB_H_

#ifdef __cplusplus
#include <cstdint>
#include <cstdio>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * struct sk_buff - Socket Buffer
 * @len: Length of the payload in the buffer.
 * @data: Points to the beginning of the payload.
 * @head: Points to the start of the allocated buffer.
 * @tail: Points to to the end of the payload.
 * @end: Points to the end of the allocated buffer.
 */
struct sk_buff {
	unsigned len;
	uint8_t *data;
	uint8_t *head;
	uint8_t *tail;
	uint8_t *end;
};

/**
 * skb_new() - Initialize a `&struct sk_buff` with a given buffer.
 * @skb: Socket buffer that will be initialized.
 * @buffer: Allocated memory that can be used by the socket buffer.
 * @total_len: Length of the memory given to the socket buffer.
 */
void skb_new(struct sk_buff *skb, uint8_t *buffer, unsigned total_len);
/**
 * skb_new_clone() - Copies the metadata of a socket buffer.
 * @skb: Copy into this target.
 * @buffer: Allocated memory that can be used by the socket buffer.
 *          The buffer must have the same size as toe buffer of the copied socket buffer.
 * @oldskb: Copy the metadata from this source.
 *
 * This function does not copy the content of the socket buffer, but only the information
 * about the used payload. The content of the buffer can be copied with ``memcpy`` either
 * before or after the call to clone.
 */
void skb_new_clone(struct sk_buff *skb, uint8_t *buffer, const struct sk_buff *oldskb);
/**
 * skb_reserve() - Reserve space for future header information.
 * @skb: Socket buffer where space will be reserved.
 * @header_len: Reserve this size for future use.
 *
 * Before calling this function, there must be enough space in the socket buffer to
 * reserve the requested header length. Reserved space can later be used to call
 * skb_push() to fill in the data. But it is not mandatory to fill this space.
 * Because of these semantics, it is not possible to reserve space at a specific
 * location in the payload, and skb_reserve() can only be called when the packet is
 * still empty.
 */
void skb_reserve(struct sk_buff *skb, unsigned header_len);

/**
 * skb_headroom() - Show the available memory at the head of the payload.
 * @skb: Socket buffer to check.
 *
 * Return: Number of unused bytes at the start of the buffer.
 */
unsigned skb_headroom(const struct sk_buff *skb);
/**
 * skb_tailroom() - Show the available memory at the end of the payload.
 * @skb: Socket buffer to check.
 *
 * Return: Number of unused bytes at the end of the buffer.
 */
unsigned skb_tailroom(const struct sk_buff *skb);
/**
 * pskb_may_pull() - Check if the requested number of bytes can be retrieved.
 * @skb: Socket buffer to check.
 * @len: Requested number of bytes to read from the buffer.
 *
 * Returns:
 * 0 if less than @len bytes available in the payload;
 * 1 if at least @len bytes can be pulled from the payload.
 */
int pskb_may_pull(const struct sk_buff *skb, unsigned len);

/**
 * skb_push() - Push data to the reserved header space of the socket buffer.
 * @skb: Socket buffer to modify.
 * @len: Number of bytes to mark as used.
 *
 * This function must be called when data is written to the header of the
 * packet.  The usual procedure is to first check, if enough space is available
 * with pskb_may_pull(), then call skb_push() and then write the data to the
 * pointer returned by this function.
 *
 * Returns: Pointer to the new start of the payload.
 */
void *skb_push(struct sk_buff *skb, unsigned len);
/**
 * skb_pull() - Remove data from the beginning of the payload.
 * @skb: Socket buffer to modify.
 * @len: Number of bytes to read from the socket buffer.
 *
 * This function must be called after data has been read from the header of the
 * packet.  By calling this function, the higher layer can assume that its
 * header start at the beginning of the payload managed by the socket buffer.
 * The usual procedure is to check if enough space is available with
 * skb_headroom(), then read the payload from the &sk_buff->data pointer
 * and then move the head by calling skb_pull().
 *
 * Returns: Pointer to the new start of the payload.
 */
void *skb_pull(struct sk_buff *skb, unsigned len);
/**
 * skb_put() - Add data to the end of the payload.
 * @skb: Socket buffer to modify.
 * @len: Number of bytes to use at the end of the packet.
 *
 * This function marks available space at the end of the packet as used.
 * This must be called when data is appended to the packet. The usual procedure is to
 * first check if enough data is available at the end with skb_tailroom(), then
 * write the data to the &sk_buff->tail pointer and then move the tail by calling
 * skb_put().
 *
 * Returns: Pointer to the new end of the payload.
 */
void *skb_put(struct sk_buff *skb, unsigned len);
/**
 * skb_trim() - Remove data from the end of the payload.
 * @skb: Socket buffer to modify.
 * @len: Number of bytes to remove from the end of the packet.
 *
 * This function must be called after data is consumed from the end of the payload.
 * The usual procedure is to call skb_trim() and then use the returned pointer to
 * read the data.
 *
 * Returns: Pointer to the new end of the payload.
 */
void skb_trim(struct sk_buff *skb, unsigned len);

void skb_push_u8(struct sk_buff *skb, uint8_t value);
void skb_push_u16(struct sk_buff *skb, uint16_t value);
void skb_push_u32(struct sk_buff *skb, uint32_t value);

void skb_put_u8(struct sk_buff *skb, uint8_t value);
void skb_put_u16(struct sk_buff *skb, uint16_t value);
void skb_put_u32(struct sk_buff *skb, uint32_t value);

uint8_t skb_pull_u8(struct sk_buff *skb);
uint16_t skb_pull_u16(struct sk_buff *skb);
uint32_t skb_pull_u32(struct sk_buff *skb);

/**
 * skb_trim_zeros() - Remove zero bytes from the end of the packet.
 * @skb: Socket buffer to trim.
 *
 * This function basically performs a very simple compression of the payload by removing
 * the zeros at the end. This requires, that the receiver knows the actual length of the
 * payload, for example by writing it to a header.
 */
void skb_trim_zeros(struct sk_buff *skb);
/**
 * skb_put_zeros() - Append zeros at the end of the
 * @skb: Socket buffer to modify.
 * @total_len: Fill the payload with zeros up to this number of bytes.
 *
 * This function is the reverse of the skb_trim_zeros() function. It fills up the
 * socket buffer to the requested @total_len by appending zeros. This can be used to
 * get the packet to a minimum required length, for example to restore expected headers.
 */
void skb_put_zeros(struct sk_buff *skb, unsigned total_len);

/**
 * skb_str() - Creates a string representation of the socket buffer.
 * @skb: Socket buffer to write to a string.
 *
 * This function will write the metadata of the socket buffer into the string and also
 * part of the payload. This function is intended only for debugging, to easily observe
 * the content of the headers in a socket buffer.
 *
 * Returns: Pointer to a string representation of the socket buffer.
 */
const char *skb_str(const struct sk_buff *skb);
/**
 * skb_print() - Write a string representation of the socket buffer to a file.
 * @file: File to write to.
 * @skb: Socket buffer to write.
 *
 * This function writes the metadata of the socket buffer and the beginning of the
 * payload.
 */
void skb_print(FILE *file, const struct sk_buff *skb);
/**
 * skb_print() - Write a string representation of the socket buffer to a file.
 * @file: File to write to.
 * @skb: Socket buffer to write.
 * @bytes: Total number of bytes in the socket buffer to write to the file.
 * @bytes_per_block: Align the bytes into blocks of this size.
 * @blocks_per_line: Print this number blocks per line.
 */
void skb_print_part(FILE *file, const struct sk_buff *skb, unsigned bytes, unsigned bytes_per_block, unsigned blocks_per_line);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_SKB_H_ */

