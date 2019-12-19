#include <stdint.h>
#include <stddef.h>
#include <x86intrin.h>

#include "binary8.h"
#include "binary8_simd.h"

int binary8_ssse3_region_add(uint8_t * restrict dest, const uint8_t * restrict src, size_t len)
{
	unsigned steps = len / sizeof(__m128i);
	unsigned tail = steps * sizeof(__m128i);

	__m128i *mm_dest = (__m128i *)dest;
	__m128i *mm_src = (__m128i *)src;

	union {
		__m128i vec;
		uint32_t nat[4];
	} ret;
	ret.vec = _mm_set1_epi32(0);

	for (unsigned i = 0; i < steps; ++i) {
		__m128i l = _mm_loadu_si128(mm_dest+i);
		__m128i r = _mm_loadu_si128(mm_src+i);
		__m128i x = _mm_xor_si128(l, r);
		ret.vec = _mm_or_si128(ret.vec, x);
		_mm_storeu_si128(mm_dest+i, x);
	}

	int r = ret.nat[0] | ret.nat[1] | ret.nat[2] | ret.nat[3];
	for (unsigned j = tail; j < len; ++j) {
		dest[j] = binary8_add(dest[j], src[j]);
		r |= dest[j];
	}

	return r != 0;
}

void binary8_ssse3_region_multiply(uint8_t *dest, uint8_t factor, size_t len)
{
	union binary8_simd_table *hi_table = &binary8_simd_table_hi[factor];
	union binary8_simd_table *lo_table = &binary8_simd_table_lo[factor];

	unsigned steps = len / sizeof(__m128i);
	unsigned tail = steps * sizeof(__m128i);

	__m128i hi_row = _mm_set_epi64x(hi_table->qwords[1], hi_table->qwords[0]);
	__m128i lo_row = _mm_set_epi64x(lo_table->qwords[1], lo_table->qwords[0]);

	__m128i mask = _mm_set1_epi8((char)0x0f);
	__m128i *mm_dest = (__m128i *)dest;

	for (unsigned i = 0; i < steps; ++i) {
		__m128i src = _mm_loadu_si128(mm_dest+i);

		__m128i lo_masked = _mm_and_si128(src, mask);
		__m128i lo_mul = _mm_shuffle_epi8(lo_row, lo_masked);

		__m128i hi = _mm_srli_epi64(src, 4);
		__m128i hi_masked = _mm_and_si128(hi, mask);
		__m128i hi_mul = _mm_shuffle_epi8(hi_row, hi_masked);

		__m128i result = _mm_xor_si128(hi_mul, lo_mul);
		_mm_storeu_si128(mm_dest+i, result);
	}

	for (unsigned j = tail; j < len; ++j) {
		dest[j] = binary8_multiply(factor, dest[j]);
	}
}

int binary8_ssse3_region_multiply_add(uint8_t * restrict dest, const uint8_t * restrict src, uint8_t factor, size_t len)
{
	union binary8_simd_table *hi_table = &binary8_simd_table_hi[factor];
	union binary8_simd_table *lo_table = &binary8_simd_table_lo[factor];

	unsigned steps = len / sizeof(__m128i);
	unsigned tail = steps * sizeof(__m128i);

	__m128i hi_row = _mm_set_epi64x(hi_table->qwords[1], hi_table->qwords[0]);
	__m128i lo_row = _mm_set_epi64x(lo_table->qwords[1], lo_table->qwords[0]);

	__m128i mask = _mm_set1_epi8((char)0x0f);
	__m128i *mm_dest = (__m128i *)dest;
	__m128i *mm_src = (__m128i *)src;

	union {
		__m128i vec;
		uint32_t nat[4];
	} ret;
	ret.vec = _mm_set1_epi32(0);

	for (unsigned i = 0; i < steps; ++i) {
		__m128i cur = _mm_loadu_si128(mm_src+i);

		__m128i lo_masked = _mm_and_si128(cur, mask);
		__m128i lo_mul = _mm_shuffle_epi8(lo_row, lo_masked);

		__m128i hi = _mm_srli_epi64(cur, 4);
		__m128i hi_masked = _mm_and_si128(hi, mask);
		__m128i hi_mul = _mm_shuffle_epi8(hi_row, hi_masked);

		__m128i prod = _mm_xor_si128(hi_mul, lo_mul);

		__m128i l = _mm_loadu_si128(mm_dest+i);
		__m128i x = _mm_xor_si128(l, prod);
		ret.vec = _mm_or_si128(ret.vec, x);
		_mm_storeu_si128(mm_dest+i, x);
	}

	int r = ret.nat[0] | ret.nat[1] | ret.nat[2] | ret.nat[3];
	for (unsigned j = tail; j < len; ++j) {
		dest[j] ^= binary8_multiply(factor, src[j]);
		r |= dest[j];
	}
	return r != 0;
}
