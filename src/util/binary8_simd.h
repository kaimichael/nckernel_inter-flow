#pragma once

#include <nckernel/config.h>

union binary8_simd_table {
	uint8_t bytes[16];
	uint16_t words[8];
	uint32_t dwords[4];
	uint64_t qwords[2];
};

extern union binary8_simd_table binary8_simd_table_lo[256];
extern union binary8_simd_table binary8_simd_table_hi[256];

void binary8_simd_init();

#ifdef USE_SSSE3
int binary8_ssse3_region_add(uint8_t * restrict dest, const uint8_t * restrict src, size_t len);
void binary8_ssse3_region_multiply(uint8_t *dest, uint8_t factor, size_t len);
int binary8_ssse3_region_multiply_add(uint8_t * restrict dest, const uint8_t * restrict src, uint8_t factor, size_t len);
#endif

#ifdef USE_AVX2
int binary8_avx2_region_add(uint8_t * restrict dest, const uint8_t * restrict src, size_t len);
void binary8_avx2_region_multiply(uint8_t *dest, uint8_t factor, size_t len);
int binary8_avx2_region_multiply_add(uint8_t * restrict dest, const uint8_t * restrict src, uint8_t factor, size_t len);
#endif
