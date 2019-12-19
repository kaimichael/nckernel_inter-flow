#include <fifi/default_field.hpp>

#include "finite_field.h"

using binary8_t = fifi::default_field<fifi::binary8>::type;

static binary8_t *binary8 = NULL;

void binary8_init()
{
	if (binary8 == NULL) {
		binary8 = new binary8_t();
	}
}

void binary8_region_multiply(uint8_t *dest, uint8_t factor, size_t len)
{
	binary8->region_multiply_constant(dest, factor, len);
}

void binary8_region_multiply_add(uint8_t *dest, uint8_t *src, uint8_t factor, size_t len)
{
	binary8->region_multiply_add(dest, src, factor, len);
}

void binary8_region_multiply_subtract(uint8_t *dest, uint8_t *src, uint8_t factor, size_t len)
{
	binary8->region_multiply_subtract(dest, src, factor, len);
}

uint8_t binary8_add(uint8_t a, uint8_t b)
{
	return binary8->add(a, b);
}

uint8_t binary8_subtract(uint8_t left, uint8_t right)
{
	return binary8->subtract(left, right);
}

uint8_t binary8_multiply(uint8_t a, uint8_t b)
{
	return binary8->multiply(a, b);
}

uint8_t binary8_divide(uint8_t divident, uint8_t divisor)
{
	return binary8->divide(divident, divisor);
}

uint8_t binary8_invert(uint8_t value)
{
	return binary8->invert(value);
}
