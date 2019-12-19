#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <x86intrin.h>

#include <nckernel/config.h>

#include "binary8.h"
#include "binary8_simd.h"

union binary8_simd_table binary8_simd_table_lo[256];
union binary8_simd_table binary8_simd_table_hi[256];

void binary8_simd_init()
{
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		for (int i = 0; i < 256; ++i) {
			for (int j = 0; j < 16; ++j) {
				uint8_t lo = binary8_multiply(i, j);
				binary8_simd_table_lo[i].bytes[j] = lo;

				uint8_t hi = binary8_multiply(i, j << 4);
				binary8_simd_table_hi[i].bytes[j] = hi;
			}
		}
	}

	if (0) {
		// this is only here to make the preprocessor work
#ifdef USE_AVX2
	} else if (__builtin_cpu_supports("avx2")) {
		binary8_region_add = binary8_avx2_region_add;
		binary8_region_multiply = binary8_avx2_region_multiply;
		binary8_region_multiply_add = binary8_avx2_region_multiply_add;
#endif
#ifdef USE_SSSE3
	} else if (__builtin_cpu_supports("ssse3")) {
		binary8_region_add = binary8_ssse3_region_add;
		binary8_region_multiply = binary8_ssse3_region_multiply;
		binary8_region_multiply_add = binary8_ssse3_region_multiply_add;
#endif
	}
}
