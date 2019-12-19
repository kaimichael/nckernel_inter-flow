/* Non-linear ring buffer block management
 *
 * Copyright (c) 2017, Sven Eckelmann <sven@narfation.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __RBUFMGR_H__
#define __RBUFMGR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#define __inline__ __inline
#endif

/**
 * struct rbufmgr - buffer management object
 * @max_blocks: Maximum blocks managed by rbufmgr
 * @num_blocks: number of blocks currently in rbufmgr
 * @write_seqno: last sequence number written to rbufmgr
 * @write_index: position of the next write when inserting @write_seqno +1
 * @read_index: position of the next read
 *
 * A non-linear ring buffer block management object stores the currently
 * available free blocks in a ringbuffer, the current write position and the
 * next read position. The buffer is not filled in a linear fashion but sequence
 * numbers are used to detect whether incoming blocks are outdated, to move the
 * current ringbuffer window or to add older entries (which are still in the
 * current ringbuffer window).
 *
 * External functionality must provide the actual buffer read/write
 * functionality and mark whether a block was actually received or not.
 */
struct rbufmgr {
	size_t max_blocks;
	size_t num_blocks;
	uint32_t write_seqno;
	size_t write_index;
	size_t read_index;
};

/**
 * rbufmgr_init() - initialize rbufmgr
 * @rbufmgr: rbufmgr to modify
 * @max_blocks: maximum number of blocks in ring buffer
 * @start_seqno: first sequence number which is expected and which will used
 *  index[0] for the first write
 */
static __inline__ void rbufmgr_init(struct rbufmgr *rbufmgr, size_t max_blocks,
				    uint32_t start_seqno)
{
	rbufmgr->max_blocks = max_blocks;
	rbufmgr->num_blocks = 0;
	rbufmgr->write_seqno = start_seqno - 1;
	rbufmgr->write_index = 0;
	rbufmgr->read_index = 0;
}

/**
 * rbufmgr_empty() - check whether there is no readable entry
 * @rbufmgr: rbufmgr to check
 *
 * Return: true when it is not allowed to call rbufmgr_read or rbufmgr_peek,
 *  false otherwise
 */
static __inline__ bool rbufmgr_empty(const struct rbufmgr *rbufmgr)
{
	return rbufmgr->num_blocks == 0;
}

/**
 * rbufmgr_full() - check whether there is no free writable entry after the
 *  current write seqno
 * @rbufmgr: rbufmgr to check
 *
 * Return: true when the next rbufmgr_insert with write_seqno + 1 will overwrite
 * the current read index, false otherwise
 */
static __inline__ bool rbufmgr_full(const struct rbufmgr *rbufmgr)
{
	return rbufmgr->num_blocks == rbufmgr->max_blocks;
}

/**
 * rbufmgr_peek() - get next entry to read but don't remove it
 * @rbufmgr: rbufmgr to check
 *
 * WARNING: The rbufmgr must be in the state !rbufmgr_empty() before this
 * function can be called
 *
 * Return: index to read from the buffer
 */
static __inline__ size_t rbufmgr_peek(const struct rbufmgr *rbufmgr)
{
	return rbufmgr->read_index;
}

/**
 * rbufmgr_read_seqno() - get sequence number which will be read next
 * @rbufmgr: rbufmgr to check
 *
 * WARNING: The rbufmgr should be in the state !rbufmgr_empty() before this
 * function can be called
 *
 * Return: sequence number which will be accesssed with next rbufmgr_peek or
 *  rbufmgr_read
 */
static __inline__ uint32_t rbufmgr_read_seqno(const struct rbufmgr *rbufmgr)
{
	return rbufmgr->write_seqno - rbufmgr->num_blocks;
}

/**
 * rbufmgr_read() - get next entry to read and remove it
 * @rbufmgr: rbufmgr to modify
 *
 * WARNING: The rbufmgr must be in the state !rbufmgr_empty() before this
 * function can be called
 *
 * Return: index to read from the buffer
 */
static __inline__ size_t rbufmgr_read(struct rbufmgr *rbufmgr)
{
	size_t cur_index;

	if (rbufmgr_empty(rbufmgr))
		return 0;

	cur_index = rbufmgr->read_index;
	rbufmgr->read_index = (rbufmgr->read_index + 1) % rbufmgr->max_blocks;
	rbufmgr->num_blocks--;

	return cur_index;
}

/**
 * rbufmgr_write_distance() - get distance between last written seqno
 *  and new seqno
 * @rbufmgr: rbufmgr to check
 * @seqno: sequence number to insert
 *
 * Return: 0 when @seqno is equal to write_seqno, < 0 when @seqno is older than
 *  write_seqno and > 0 when @seqno is newer than @write_seqno
 */
static __inline__ int32_t rbufmgr_write_distance(const struct rbufmgr *rbufmgr,
						 uint32_t seqno)
{
	return seqno - rbufmgr->write_seqno;
}

/**
 * rbufmgr_outdated() - check whether sequence number is outside window
 * @rbufmgr: rbufmgr to check
 * @seqno: sequence number to insert
 *
 * WARNING: Must be checked before calling rbufmgr_insert.
 *
 * Return: true when it is not allowed to call rbufmgr_insert, false otherwise
 */
static __inline__ bool rbufmgr_outdated(const struct rbufmgr *rbufmgr,
					uint32_t seqno)
{
	int32_t distance;
	size_t older_than_write_seqno;

	/* is the new seqno newer than the write seqno -> cannot be outdated */
	distance = rbufmgr_write_distance(rbufmgr, seqno);
	if (distance > 0)
		return false;

	/* when the seqno is older than the last retrieved read index sequence
	 * number -> outdated
	 */
	older_than_write_seqno = -distance;
	return older_than_write_seqno >= rbufmgr->num_blocks;
}

/**
 * rbufmgr_shift_distance() - calculate number of sequence number and readable
 *  blocks which will be lost after insert
 * @rbufmgr: rbufmgr to check
 * @seqno: sequence number to insert
 * @read_index: first index which will be lost after the rbufmgr_insert
 * @lost_read_blocks: number of read_blocks which will be lost
 *
 * Return: distance between most recent sequence number when the seqno is newer,
 *  otherwise 0
 */
static __inline__ size_t rbufmgr_shift_distance(const struct rbufmgr *rbufmgr,
						uint32_t seqno,
						size_t *read_index,
						size_t *lost_read_blocks)
{
	int32_t distance;
	size_t seqno_shift;
	size_t free_blocks;

	if (read_index)
		*read_index = rbufmgr->read_index;

	/* new sequence number is older (or as old as the current seqno)
	 * and ringbuffer window will therefore not be shifted
	 */
	distance = rbufmgr_write_distance(rbufmgr, seqno);
	if (distance <= 0) {
		if (lost_read_blocks)
			*lost_read_blocks = 0;

		return 0;
	}

	/* we already know that is newer than the current write_seqno and
	 * thus the distance is >= 0
	 */
	seqno_shift = distance;
	if (lost_read_blocks) {
		/* a non-full ring buffer can compensate parts of the
		 * seqno_shift in its free blocks
		 */
		free_blocks = rbufmgr->max_blocks - rbufmgr->num_blocks;

		/* if the seqno shift is larger than the number of blocks
		 * managed by rbufmgr then drop all the blocks currently managed
		 * (readable) by rbufmgr.
		 *
		 * if the seqno shift is larger then the number of unused blocks
		 * then drop available (readable) blocks until enough are free
		 * to compensate the seqno shift.
		 *
		 * otherwise the free blocks can compensate the seqno shift
		 * and nothing has to be dropped
		 */
		if (seqno_shift >= rbufmgr->max_blocks)
			*lost_read_blocks = rbufmgr->num_blocks;
		else if (seqno_shift > free_blocks)
			*lost_read_blocks = seqno_shift - free_blocks;
		else
			*lost_read_blocks = 0;
	}

	return seqno_shift;
}

/**
 * rbufmgr_insert() - insert block with a specific sequence number
 * @rbufmgr: rbufmgr to modify
 * @seqno: sequence number to insert
 *
 * WARNING: rbufmgr_outdated must be called before calling rbufmgr_insert.
 *  Otherwise the returned write index may be wrong
 *
 * WARNING: It is possible that a ringbuffer overflows when adding a large
 *  enough seqno. It is therefore recommended to get the read indexes, which
 *  will be removed from the window, via rbufmgr_shift_distance().
 *
 * Return: index of block which can be filled
 */
static __inline__ size_t rbufmgr_insert(struct rbufmgr *rbufmgr, uint32_t seqno)
{
	size_t seqno_shift;
	int32_t distance;
	size_t write_index;
	size_t free_blocks;

	if (rbufmgr_outdated(rbufmgr, seqno))
		return 0;

	seqno_shift = rbufmgr_shift_distance(rbufmgr, seqno, NULL, NULL);

	/* new seqno is not outdated and doesn't require shifting -> overwrites
	 * old entry
	 */
	if (seqno_shift == 0) {
		distance = rbufmgr_write_distance(rbufmgr, seqno);

		/* the write index (x) must be 0 <= x <= max_blocks - 1
		 * we should now calculate where the old index was. The offset
		 * for this must now be calculated inside the ring via positive
		 * adds to avoid problems when max_blocks is not a power of two.
		 *
		 * if distance is 0 then it is left (on the ring) of the *next*
		 * write_index. The left index of this known index can
		 * now be calculated by adding "rbufmgr->max_blocks - 1" to the
		 * index and then using modulus to make sure that the result
		 * stays inside the ring
		 */

		/* calculate the relative offset */
		write_index = rbufmgr->max_blocks - 1 + distance;
		write_index %= rbufmgr->max_blocks;

		/* calculate the absolute index inside the ring buffer */
		write_index += rbufmgr->write_index;
		write_index %= rbufmgr->max_blocks;

		return write_index;
	}

	/* shift the expected next write index to point to the position after
	 * our current write
	 */
	rbufmgr->write_index += seqno_shift;
	rbufmgr->write_index %= rbufmgr->max_blocks;
	rbufmgr->write_seqno = seqno;

	/* the actual write index is now left (in the ring) of the next write
	 * index. The left index of this known index can now be calculated by
	 * adding "rbufmgr->max_blocks - 1" to the index and then using modulus
	 * to make sure that the result stays inside the ring.
	 *
	 * The index must be calculated inside the ring via positive adds to
	 * avoid problems when max_blocks is not a power of two.
	 */
	write_index = rbufmgr->max_blocks - 1 + rbufmgr->write_index;
	write_index %= rbufmgr->max_blocks;

	free_blocks = rbufmgr->max_blocks - rbufmgr->num_blocks;
	if (free_blocks >= seqno_shift) {
		/* if the shift is smaller or equal to the free blocks then it
		 * the read_index doesn't have to be touched because the new
		 * entry will be added to free blocks
		 */
		rbufmgr->num_blocks += seqno_shift;
	} else {
		/* otherwise the ring buffer is full and the read index is right
		 * (in the ring) of the current write_index (which is also the
		 * next expected write index)
		 */
		rbufmgr->num_blocks = rbufmgr->max_blocks;
		rbufmgr->read_index = rbufmgr->write_index;
	}

	return write_index;
}

#ifdef __cplusplus
}
#endif

#endif /* __RBUFMGR_H__ */
