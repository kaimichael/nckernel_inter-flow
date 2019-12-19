#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "binary8.h"

#ifdef USE_SIMD
void binary8_simd_init();
#endif

#ifdef USE_TABLES
void binary8_table_init();
#endif

uint8_t (*binary8_invert)(uint8_t value);
uint8_t (*binary8_multiply)(uint8_t left, uint8_t right);

int (*binary8_region_add)(uint8_t * restrict dest, const uint8_t * restrict other, size_t len);
void (*binary8_region_multiply)(uint8_t *dest, uint8_t factor, size_t len);
int (*binary8_region_multiply_add)(uint8_t * restrict dest, const uint8_t * restrict other, uint8_t factor, size_t len);
void (*binary8_region_multiply_sum)(uint8_t *dest, const uint8_t **srcs, const uint8_t *factors, size_t count, size_t len);

static uint_fast16_t clmul8(uint_fast8_t left, uint_fast8_t right)
{
	uint_fast16_t ret = 0;
	for (uint_fast16_t shift = 1; shift < 0x100; shift <<= 1) {
		ret ^= (right&shift)*left;
	}
	return ret;
}

static uint_fast64_t clmul4vec8(uint_fast64_t left, uint_fast8_t right)
{
	uint_fast64_t ret = 0;
	for (uint_fast16_t shift = 1; shift < 0x100; shift <<= 1) {
		ret ^= (right&shift)*left;
	}
	return ret;
}

static uint8_t binary8_online_multiply(uint8_t left, uint8_t right)
{
	// carry-less multiplication
	uint_fast16_t prod = clmul8(left, right);

	// reduction modulo prime
	uint_fast8_t c = prod>>8;
	// carry-less multiply with q+
	uint_fast8_t a = (((c<<8) ^ (c<<4) ^ (c<<3) ^ (c<<2))>>8) & 0xff;
	// carry-less multiply with q*
	uint_fast8_t b = (a<<4) ^ (a<<3) ^ (a<<2) ^ a;

	return b ^ prod;
}

#if HAS_BUILTIN_CLZ
static inline int degree(unsigned value) {
	return sizeof(unsigned)*8 - __builtin_clz(value) - 1;
}
#else
static inline int degree(unsigned value) {
	int_fast8_t ret = 0;
	for (ret = 0; value>>(ret+1); ++ret)
	{ }
	return ret;
}
#endif

static uint8_t binary8_online_invert(uint8_t value)
{
	uint_fast8_t t = 0, newt = 1, tmp;
	uint_fast8_t r = (uint_fast8_t)binary8_prime, newr = value;
	int_fast8_t d;

	if (value == 1 || value == 0) {
		return value;
	}

	// In the first iteration the prime has degree 8.
	// Since it is possible that this is not representable in our values,
	// we do the first iteration manually.
	d = 8 - degree(newr);
	assert(d > 0);

	r ^= (newr << d);
	t ^= (newt << d);

	while (r != 1) {
		d = degree(r) - degree(newr);

		if (d < 0) {
			tmp = r;
			r = newr;
			newr = tmp;

			tmp = t;
			t = newt;
			newt = tmp;

			d = -d;
		}

		r ^= (newr << d);
		t ^= (newt << d);
	}

	return t;
}

static void binary8_online_region_multiply(uint8_t *dest, uint8_t factor, size_t len)
{
	union {
		uint_fast64_t vec;
		uint8_t bytes[8];
	} acc;

	uint_fast8_t step = sizeof(uint_fast64_t)/2;
	uint_fast64_t mask = 0xff;
	for (uint_fast8_t j = 0; j < step; ++j) {
		mask = (mask<<16) | 0xff;
	}

	for (uint_fast32_t i = 0; i < len; i += 4) {
		acc.vec = 0;
		for (uint_fast8_t j = 0; j < step; ++j) {
			acc.bytes[j*2] = dest[i+j];
		}

		// carry-less multiplication
		uint_fast64_t prod = clmul4vec8(acc.vec, factor);

		// reduction modulo prime
		acc.vec = (prod >> 8) & mask;

		// carry-less multiply with q+
		acc.vec = ((acc.vec<<8) ^ (acc.vec<<4) ^ (acc.vec<<3) ^ (acc.vec<<2)) >> 8;
		acc.vec = acc.vec & mask;

		// carry-less multiply with q*
		acc.vec = (acc.vec<<4) ^ (acc.vec<<3) ^ (acc.vec<<2) ^ acc.vec;

		// xor with lsb
		acc.vec ^= prod;

		for (uint_fast8_t j = 0; j < step; ++j) {
			dest[i+j] = acc.bytes[j*2];
		}
	}
}

static int binary8_simple_region_add(uint8_t * restrict dest, const uint8_t * restrict src, size_t len)
{
	int ret = 0;
	for (size_t i = 0; i < len; ++i) {
		dest[i] ^= src[i];
		ret |= dest[i];
	}
	return ret != 0;
}

static int binary8_simple_region_multiply_add(uint8_t * restrict dest, const uint8_t * restrict src, uint8_t factor, size_t len)
{
	int ret = 0;
	uint8_t cache[512];

	unsigned steps = len / sizeof(cache);
	unsigned tail = len % sizeof(cache);

	for (unsigned i = 0; i < steps; ++i) {
		memcpy(cache, src+i*sizeof(cache), sizeof(cache));
		binary8_region_multiply(cache, factor, sizeof(cache));
		ret |= binary8_region_add(dest+i*sizeof(cache), cache, sizeof(cache));
	}

	if (tail) {
		memcpy(cache, src+steps*sizeof(cache), tail);
		binary8_region_multiply(cache, factor, tail);
		ret |= binary8_region_add(dest+steps*sizeof(cache), cache, tail);
	}

	return ret != 0;
}

static void binary8_simple_region_multiply_sum(uint8_t *dest, const uint8_t **srcs, const uint8_t *factors, size_t count, size_t len)
{
	for (unsigned i = 0; i < count; ++i) {
		binary8_region_multiply_add(dest, srcs[i], factors[i], len);
	}
}

void binary8_seed(struct binary8_generator *gen, const uint8_t *seed, size_t seed_size)
{
	gen->index = 0;
	if (seed && seed_size > 0) {
		if (seed_size > sizeof(gen->state)) {
			seed_size = sizeof(gen->state);
		}

		memcpy(gen->state, seed, seed_size);
	}
}

uint8_t binary8_get(struct binary8_generator *gen)
{
	if (gen->index >= sizeof(gen->random)) {
		binary8_roll(gen);
	}

	return gen->random.bytes[gen->index++];
}

void binary8_fill(struct binary8_generator *gen, uint8_t *dest, size_t len)
{
	if (gen->index < sizeof(gen->random)) {
		unsigned copy = sizeof(gen->random) - gen->index;
		if (copy < len) {
			memcpy(dest, &gen->random.bytes[gen->index], copy);
			dest += copy;
			len -= copy;
		} else {
			memcpy(dest, &gen->random.bytes[gen->index], len);
			gen->index += len;
			return;
		}
	}

	while (len > sizeof(gen->random)) {
		binary8_roll(gen);
		memcpy(dest, &gen->random, sizeof(gen->random));
		len -= sizeof(gen->random);
		dest += sizeof(gen->random);
	}

	binary8_roll(gen);
	memcpy(dest, &gen->random, len);
	gen->index = len;
}

void binary8_roll(struct binary8_generator *gen)
{
	uint64_t x = gen->state[0];
	uint64_t y = gen->state[1];
	gen->state[0] = y;
	x ^= x << 23;
	gen->state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
	gen->random.number = gen->state[1] + y;
	gen->index = 0;
}

void binary8_init()
{
	binary8_invert = binary8_online_invert;
	binary8_multiply = binary8_online_multiply;

	binary8_region_multiply = binary8_online_region_multiply;
	binary8_region_add = binary8_simple_region_add;
	binary8_region_multiply_add = binary8_simple_region_multiply_add;
	binary8_region_multiply_sum = binary8_simple_region_multiply_sum;

#ifdef USE_TABLES
	binary8_table_init();
#endif
#ifdef USE_SIMD
	binary8_simd_init();
#endif
}
