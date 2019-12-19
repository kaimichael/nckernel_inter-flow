#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <x86intrin.h>
#include <immintrin.h>

#include "binary8.h"
#include "binary8_simd.h"

int binary8_avx2_region_add(uint8_t * restrict dest, const uint8_t * restrict src, size_t len)
{
	unsigned steps = len / sizeof(__m256i);
	unsigned tail = steps * sizeof(__m256i);

	__m256i *mm_dest = (__m256i *)dest;
	__m256i *mm_src = (__m256i *)src;

	union {
		__m256i vec;
		uint32_t nat[8];
	} ret;
	ret.vec = _mm256_set1_epi32(0);

	for (unsigned i = 0; i < steps; ++i) {
		__m256i l = _mm256_loadu_si256(mm_dest+i);
		__m256i r = _mm256_loadu_si256(mm_src+i);
		__m256i x = _mm256_xor_si256(l, r);
		ret.vec = _mm256_or_si256(ret.vec, x);
		_mm256_storeu_si256(mm_dest+i, x);
	}

	int r = ret.nat[0] | ret.nat[1] | ret.nat[2] | ret.nat[3] | ret.nat[4] | ret.nat[5] | ret.nat[6] | ret.nat[7];
	for (unsigned j = tail; j < len; ++j) {
		dest[j] = binary8_add(dest[j], src[j]);
		r |= dest[j];
	}

	return 0;
}

void binary8_avx2_region_multiply(uint8_t *dest, uint8_t factor, size_t len)
{
	union binary8_simd_table *hi_table = &binary8_simd_table_hi[factor];
	union binary8_simd_table *lo_table = &binary8_simd_table_lo[factor];

	unsigned steps = len / sizeof(__m256i);
	unsigned tail = steps * sizeof(__m256i);

	__m256i hi_row = _mm256_set_epi64x(hi_table->qwords[1], hi_table->qwords[0], hi_table->qwords[1], hi_table->qwords[0]);
	__m256i lo_row = _mm256_set_epi64x(lo_table->qwords[1], lo_table->qwords[0], lo_table->qwords[1], lo_table->qwords[0]);

	__m256i mask = _mm256_set1_epi8((char)0x0f);
	__m256i *mm_dest = (__m256i*)dest;

	for (unsigned i = 0; i < steps; ++i) {
		__m256i src = _mm256_loadu_si256(mm_dest+i);

		__m256i lo_masked = _mm256_and_si256(src, mask);
		__m256i lo_mul = _mm256_shuffle_epi8(lo_row, lo_masked);

		__m256i hi = _mm256_srli_epi64(src, 4);
		__m256i hi_masked = _mm256_and_si256(hi, mask);
		__m256i hi_mul = _mm256_shuffle_epi8(hi_row, hi_masked);

		__m256i result = _mm256_xor_si256(hi_mul, lo_mul);
		_mm256_storeu_si256(mm_dest+i, result);
	}

	for (unsigned j = tail; j < len; ++j) {
		dest[j] = binary8_multiply(factor, dest[j]);
	}
}

int binary8_avx2_region_multiply_add(uint8_t * restrict dest, const uint8_t * restrict src, uint8_t factor, size_t len)
{
	union binary8_simd_table *hi_table = &binary8_simd_table_hi[factor];
	union binary8_simd_table *lo_table = &binary8_simd_table_lo[factor];

	unsigned steps = len / sizeof(__m256i);
	unsigned tail = steps * sizeof(__m256i);

	__m256i hi_row = _mm256_set_epi64x(hi_table->qwords[1], hi_table->qwords[0], hi_table->qwords[1], hi_table->qwords[0]);
	__m256i lo_row = _mm256_set_epi64x(lo_table->qwords[1], lo_table->qwords[0], lo_table->qwords[1], lo_table->qwords[0]);

	__m256i mask = _mm256_set1_epi8((char)0x0f);
	__m256i *mm_dest = (__m256i*)dest;
	__m256i *mm_src = (__m256i*)src;

	union {
		__m256i vec;
		uint32_t nat[8];
	} ret;
	ret.vec = _mm256_set1_epi32(0);

	for (unsigned i = 0; i < steps; ++i) {
		__m256i cur = _mm256_loadu_si256(mm_src+i);

		__m256i lo_masked = _mm256_and_si256(cur, mask);
		__m256i lo_mul = _mm256_shuffle_epi8(lo_row, lo_masked);

		__m256i hi = _mm256_srli_epi64(cur, 4);
		__m256i hi_masked = _mm256_and_si256(hi, mask);
		__m256i hi_mul = _mm256_shuffle_epi8(hi_row, hi_masked);

		__m256i prod = _mm256_xor_si256(hi_mul, lo_mul);
		__m256i l = _mm256_loadu_si256(mm_dest+i);
		__m256i x = _mm256_xor_si256(l, prod);
		ret.vec = _mm256_or_si256(ret.vec, x);
		_mm256_storeu_si256(mm_dest+i, x);
	}

	int r = ret.nat[0] | ret.nat[1] | ret.nat[2] | ret.nat[3] | ret.nat[4] | ret.nat[5] | ret.nat[6] | ret.nat[7];
	for (unsigned j = tail; j < len; ++j) {
		dest[j] ^= binary8_multiply(factor, src[j]);
		r |= dest[j];
	}
	return r;
}
