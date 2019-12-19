#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "binary8.h"

static uint_fast8_t mul_table[256][256];
static uint_fast8_t inv_table[256];

static uint8_t binary8_table_invert(uint8_t value)
{
	return inv_table[value];
}

static uint8_t binary8_table_multiply(uint8_t left, uint8_t right)
{
	return mul_table[left][right];
}

static void binary8_table_region_multiply(uint8_t *dest, uint8_t factor, size_t len)
{
	uint_fast8_t *line = mul_table[factor];
	for (size_t i = 0; i < len; ++i) {
		dest[i] = line[dest[i]];
	}
}

void binary8_table_init()
{
	inv_table[0] = 0;
	for (int i = 1; i <= 0xff; ++i) {
		inv_table[i] = binary8_invert(i);
		mul_table[0][i] = 0;
		for (int j = 0; j <= 0xff; ++j) {
			mul_table[i][j] = binary8_multiply(i, j);
		}
	}

	binary8_invert = binary8_table_invert;
	binary8_multiply = binary8_table_multiply;
	binary8_region_multiply = binary8_table_region_multiply;
}
