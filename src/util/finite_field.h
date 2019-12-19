#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void binary8_init();
void binary8_region_multiply(uint8_t *dst, uint8_t factor, size_t len);
void binary8_region_multiply_add(uint8_t *dest, uint8_t *src, uint8_t factor, size_t len);
void binary8_region_multiply_subtract(uint8_t *dest, uint8_t *src, uint8_t factor, size_t len);
uint8_t binary8_add(uint8_t a, uint8_t b);
uint8_t binary8_subtract(uint8_t left, uint8_t right);
uint8_t binary8_multiply(uint8_t a, uint8_t b);
uint8_t binary8_divide(uint8_t divident, uint8_t divisor);
uint8_t binary8_invert(uint8_t value);

#ifdef __cplusplus
} /* extern "C" */
#endif
