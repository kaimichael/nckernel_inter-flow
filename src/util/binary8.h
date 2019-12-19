#pragma once

static const int binary8_prime = 0x11d;

struct binary8_generator {
	unsigned index;
	uint64_t state[2];
	union {
		uint64_t number;
		uint8_t bytes[8];
	} random;
};

void binary8_init();

extern uint8_t (*binary8_invert)(uint8_t value);
extern uint8_t (*binary8_multiply)(uint8_t left, uint8_t right);

extern int (*binary8_region_add)(uint8_t * restrict dest, const uint8_t * restrict other, size_t len);
extern void (*binary8_region_multiply)(uint8_t *dest, uint8_t factor, size_t len);
extern int (*binary8_region_multiply_add)(uint8_t * restrict dest, const uint8_t * restrict other, uint8_t factor, size_t len);
extern void (*binary8_region_multiply_sum)(uint8_t *dest, const uint8_t **srcs, const uint8_t *factors, size_t count, size_t len);

void binary8_seed(struct binary8_generator *gen, const uint8_t *seed, size_t seed_size);
uint8_t binary8_get(struct binary8_generator *gen);
void binary8_fill(struct binary8_generator *gen, uint8_t *dest, size_t len);
void binary8_roll(struct binary8_generator *gen);

static inline uint8_t binary8_add(uint8_t left, uint8_t right) {
	return left ^ right;
}

static inline uint8_t binary8_subtract(uint8_t left, uint8_t right) {
	return binary8_add(left, right);
}

static inline uint8_t binary8_negate(uint8_t value) {
	return binary8_subtract(0, value);
}

static inline int binary8_region_subtract(uint8_t * restrict dest, const uint8_t * restrict other, size_t len) {
	return binary8_region_add(dest, other, len);
}

static inline int binary8_region_multiply_subtract(uint8_t * restrict dest, const uint8_t * restrict other, uint8_t factor, size_t len) {
	return binary8_region_multiply_add(dest, other, factor, len);
}

static inline uint8_t binary8_divide(uint8_t left, uint8_t right) {
	return binary8_multiply(left, binary8_invert(right));
}

static inline void binary8_region_divide(uint8_t *dest, uint8_t factor, size_t len) {
	binary8_region_multiply(dest, binary8_invert(factor), len);
}

static inline int binary8_region_divide_add(uint8_t * restrict dest, const uint8_t * restrict other, uint8_t factor, size_t len) {
	return binary8_region_multiply_add(dest, other, binary8_invert(factor), len);
}

static inline int binary8_region_divide_subtract(uint8_t * restrict dest, const uint8_t * restrict other, uint8_t factor, size_t len) {
	return binary8_region_multiply_subtract(dest, other, binary8_invert(factor), len);
}
