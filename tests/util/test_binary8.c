#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <util/binary8.h>
#include <util/binary8_simd.h>
#include <util/finite_field.h>

#define COUNT 100000000

struct benchmark {
	const char *title;
	void (*run)(uint8_t *values, size_t len);
	uint8_t results[COUNT];
};

#define BENCHMARK(fnc) { #fnc, fnc, {0} }

uint8_t numbers[COUNT];

void run(struct benchmark *target, const uint8_t * restrict values, size_t len)
{
	struct timespec start, end, duration;

	memcpy(target->results, values, len);

	clock_gettime(CLOCK_REALTIME, &start);
	target->run(target->results+7, len - 7);
	clock_gettime(CLOCK_REALTIME, &end);

	duration.tv_sec = end.tv_sec - start.tv_sec;
	duration.tv_nsec = end.tv_nsec - start.tv_nsec;
	if (duration.tv_nsec < 0) {
		duration.tv_sec -= 1;
		duration.tv_nsec += 1000000000L;
	}

	printf("%-30s: %ld.%09ld\n",
			target->title, duration.tv_sec, duration.tv_nsec);
}

void check(struct benchmark *list, size_t len) {
	for (struct benchmark *b = list+1; b->title; ++b) {
		if (memcmp(list->results, b->results, len)) {
			printf("ERROR: %s != %s\n", list->title, b->title);
		}
	}
}

void benchmark_binary8_invert(uint8_t *values, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		if (values[i]) {
			values[i] = binary8_invert(values[i]);
		}
	}
}

void benchmark_binary8_multiply(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	for (size_t i = 0; i < len; ++i) {
		values[i] = binary8_multiply(values[i], factor);
	}
}

void benchmark_binary8_region_multiply(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	binary8_region_multiply(values, factor, len);
}

void benchmark_binary8_add(uint8_t *values, size_t len)
{
	size_t half = len/2;
	for (size_t i = 0; i < half; ++i) {
		values[i] = binary8_add(values[i], values[i+half]);
	}
}

void benchmark_binary8_region_add(uint8_t *values, size_t len)
{
	size_t half = len/2;
	binary8_region_add(values, values+half, half);
}

void benchmark_binary8_region_multiply_add(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	size_t half = len/2;
	binary8_region_multiply_add(values, values+half, factor, half);
}

void benchmark_fifi_invert(uint8_t *values, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		if (values[i]) {
			values[i] = fifi_invert(values[i]);
		}
	}
}

void benchmark_fifi_multiply(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	for (size_t i = 0; i < len; ++i) {
		values[i] = fifi_multiply(values[i], factor);
	}
}

void benchmark_fifi_region_multiply(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	fifi_region_multiply(values, factor, len);
}

void benchmark_fifi_add(uint8_t *values, size_t len)
{
	size_t half = len/2;
	for (size_t i = 0; i < half; ++i) {
		values[i] = fifi_add(values[i], values[i+half]);
	}
}

void benchmark_fifi_region_add(uint8_t *values, size_t len)
{
	size_t half = len/2;
	fifi_region_add(values, values+half, half);
}

void benchmark_fifi_region_multiply_add(uint8_t *values, size_t len)
{
	uint8_t factor = values[0];
	size_t half = len/2;
	fifi_region_multiply_add(values, values+half, factor, half);
}

struct benchmark bench_invert[] = {
	BENCHMARK(benchmark_fifi_invert),
	BENCHMARK(benchmark_binary8_invert),
	{ NULL }
};

struct benchmark bench_multiply[] = {
	BENCHMARK(benchmark_fifi_multiply),
	BENCHMARK(benchmark_fifi_region_multiply),
	BENCHMARK(benchmark_binary8_multiply),
	BENCHMARK(benchmark_binary8_region_multiply),
	{ NULL }
};

struct benchmark bench_add[] = {
	BENCHMARK(benchmark_fifi_add),
	BENCHMARK(benchmark_fifi_region_add),
	BENCHMARK(benchmark_binary8_add),
	BENCHMARK(benchmark_binary8_region_add),
	{ NULL }
};

struct benchmark bench_mul_add[] = {
	BENCHMARK(benchmark_fifi_region_multiply_add),
	BENCHMARK(benchmark_binary8_region_multiply_add),
	{ NULL }
};

int main()
{
	FILE *random;

	random = fopen("/dev/urandom", "rb");
	if (!random) {
		perror("fopen");
	}

	size_t len = fread(numbers, 1, sizeof(numbers), random);

	binary8_init();
	//binary8_table_init();
	//binary8_simd_init();
	fifi_init();

	for (struct benchmark *b = bench_invert; b->title; b++) {
		run(b, numbers, len);
	}
	check(bench_invert, len);

	for (struct benchmark *b = bench_multiply; b->title; b++) {
		run(b, numbers, len);
	}
	check(bench_multiply, len);

	for (struct benchmark *b = bench_add; b->title; b++) {
		run(b, numbers, len);
	}
	check(bench_add, len);

	for (struct benchmark *b = bench_mul_add; b->title; b++) {
		run(b, numbers, len);
	}
	check(bench_mul_add, len);
}
