/**
 * @file cfix_main.c
 * @brief Test program for hash table for 32-bit entries, i.e. (key, data) pairs.
 * @author Mikael Sundstrom <micke@fabinv.com>
 *
 * @copyright Copyright (c) 2018 Fabulous Inventions AB - all rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "cfix.h"

#define N (1 << 26)
//0xfffffffe

#define M ((N / sizeof(uint64_t)) + 1) 

//#define K 1
#define K 1000

#define DATA 0

//#define STATS
//#define COMP

uint64_t *bit;

#if 0
static void
dump(uint64_t word)
{
	uint32_t bix;

	for (bix = 63;; bix--) {
		fprintf(stderr, "%u", bix % 10);
		if (bix == 0) break;
	}
	fprintf(stderr, "\n");
	for (bix = 63;; bix--) {
		fprintf(stderr, "%lu", (word >> bix) & 0x0000000000000001);
		if (bix == 0) break;
	}
	fprintf(stderr, "\n\n");
}
#endif

static void
dump_key(uint32_t key, uint32_t *data, void *aux)
{
	uint32_t *count = (uint32_t *)aux;

	++(*count);
	fprintf(stderr, "%10u %010u\n", *count, key); 
}

static void init(void)
{
	bit = (uint64_t *)calloc(M, sizeof(uint64_t));
	assert(bit != NULL);
}

static bool
get(uint32_t ix)
{
	uint64_t wix = (uint64_t)ix >> 6l , bix = (uint64_t)ix & 0x000000000000003fl;

	return (bool)((bit[wix] >> bix) & 0x0000000000000001l);
}

static void
clr(uint32_t ix)
{
	uint64_t word, mask, wix = ix >> 6l , bix = ix & 0x000000000000003fl;

	word = bit[wix];
	mask = ~(0x0000000000000001l << bix);
	word &= mask;
	
	bit[wix] = word;
	
	assert(!get(ix));
}

static void
set(uint32_t ix)
{
	uint64_t word, mask, wix = (uint64_t)ix >> 6l , bix = (uint64_t)ix & 0x000000000000003fl;

	word = bit[wix];
	mask = 0x0000000000000001l << bix;
	word |= mask;
	
	bit[wix] = word;
	
	assert(get(ix));
}

static cfix_config_t conf = {
	10,//000000,
	DATA,
	4,
	0.05,
	0.95,
	CFIX_CONFIG_DEFAULT_GROWTH,
	CFIX_CONFIG_DEFAULT_ATTEMPT,
	CFIX_CONFIG_DEFAULT_RANDOM	
};

#include <sys/time.h>

uint64_t
nanoseconds(void)
{
	uint64_t result;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	result = (uint64_t)ts.tv_nsec;
	result += (uint64_t)ts.tv_sec * 1000000000;
	return result;
}

static double
fill(cfix_t *h)
{
	return 100.0 * (double)cfix_keys(h) / ((double)cfix_bins(h) * (double)CFIX_BIN_SIZE);
}

#define BUFSIZE 4096
static char buf[BUFSIZE];

static void
compromise(uint32_t key, uint32_t *data, void *aux)
{
	cfix_t *h = (cfix_t *)aux;

	cfix_delete(h, key);
}

static uint32_t inf;
#define KEY (key == inf ? 0xffffffff : key)

int
main(int argc, char *argv[])
{
	cfix_t *h;
	uint64_t i = 0, d = 0, f = 0, s = 0, t1, t2, k;
	uint32_t key, data;
#ifdef STATS
	cfix_stats_t stats;
	uint32_t g;
#endif
	char *op;

	init();

	cfix_create(&h, &conf);

	lrand48();

	t1 = nanoseconds();
	for (k = 0; k < 8 * (N / 10); k++) {
		for (key = (uint32_t)lrand48() % N; get(key); key = (key + 1) % N);
		assert(!get(key));
		if (k == 0) inf = key;

#if 0
		if (KEY == 0xffffffff) {
			fprintf(stderr, "\nINFINITY");
			getchar();
		}
#endif

		++i;
		data = ~key;
		cfix_insert(h, KEY, &data);
		set(key);
		if (((k + 1) % K) == 0) {
			t2 = nanoseconds();
#if K > 1
			fprintf(stderr, "INSERT: %10lu updates, %10lu insertions, %10lu deletions, %10u entries in the range [%010u, %010u], %10lu nanoseconds per update, %5.3lf%% full\n", i, i, 0l, cfix_keys(h), cfix_min(h), cfix_max(h), (t2 - t1) / K, fill(h));
#else
			fprintf(stderr, "INSERT: %010u %10lu updates, %10lu insertions, %10lu deletions, %10u entries in the range [%010u, %010u], %10lu nanoseconds per update, %5.3lf%% full %10u keys\n", key, i, i, 0l, cfix_keys(h), cfix_min(h), cfix_max(h), (t2 - t1) / K, fill(h), cfix_keys(h));
#endif
#ifdef STATS
			cfix_stats(h, &stats);
			fprintf(stderr, "HISTOGRAM: ");
			for (g = 0; g <= CFIX_BIN_SIZE; g++) fprintf(stderr, "%5.2f%% ", 100.0 * (float)stats.hist[g] / (float)cfix_bins(h));
			fprintf(stderr, "\nPRIMARY: %u %.4f\n", stats.primary, 100.0 * (float)stats.primary / (float)cfix_keys(h));
			fprintf(stderr, "\n\n");
#endif
			t1 = t2;
		}
#ifdef COMP
		if (fill(h) < 75 && ((lrand48() % 100) == 1)) cfix_rebuild(h, 0.92); 
#endif
	}

#if 0
	{
		cfix_t *victim = h, *clone = cfix_clone(h);

		h = clone;
		cfix_destroy(&victim);
	}
#endif

#if 0
	{
		cfix_apply(h, compromise, (void *)h);
	}
#endif

#if N <= (1 << 16)
#	if 0
	{
		uint32_t count = 0;

		cfix_apply(h, dump_key, (void *)&count);
		getchar();
	}
#	else
	{
		cfix_iter_t *iter;
		uint32_t count = 0;

		cfix_iter_create(h, &iter);

		do {
			uint32_t k, d[666];

			assert(cfix_iter_current(h, iter, &k, d) == CFIX_ITER_SUCCESS);
			fprintf(stderr, "%10u %010u\n", ++count, k);
		} while (cfix_iter_forward(h, iter) == CFIX_ITER_SUCCESS);

		fprintf(stderr, "keys = %u, count = %u\n", cfix_keys(h), count);
		assert(cfix_keys(h) == count);
		cfix_iter_destroy(&iter);
	}
#	endif
#endif

	t1 = nanoseconds();
	for (k = 0; k < N; k++) {
		key = (uint32_t)(lrand48() % N);
		if (cfix_lookup(h, KEY, &data)) {
#if DATA > 0
			assert(data == ~key);
#endif
			++s;
		} else {
			++f;
		}
		if (((k + 1) % K) == 0) {
			t2 = nanoseconds();
			fprintf(stderr, "LOOKUP: %10lu lookups, %10lu successful, %10lu failures, %10lu nanoseconds per lookup\n", f + s, s, f, (t2 - t1) / K);
			t1 = t2;
		}
	}

	t1 = nanoseconds();
	for (k = 0; k < N; k++) {
		key = (uint32_t)(lrand48() % N);
		if (get(key)) {
			assert(cfix_lookup(h, KEY, &data));
			++d;
			assert(cfix_delete(h, KEY));
			clr(key);
			op = "deletion";
		} else {
			assert(!cfix_lookup(h, KEY, &data));
			++i;
			data = ~key;
			cfix_insert(h, KEY, &data);
			set(key);
			op = "insertion";
		}
		if (i - d != cfix_keys(h)) {
			fprintf(stderr, "Entry count mismatch: insertions = %lu, deletions = %lu, projected entries = %lu, actual entries = %u, last entry = 0x%08x, last operation = %s\n",
				   i, d, i - d, cfix_keys(h), key, op);
			assert(0);	
		}
		if (((k + 1) % K) == 0) {
			t2 = nanoseconds();
			fprintf(stderr, "UPDATE: %10lu updates, %10lu insertions, %10lu deletions, %10u entries in the range [%010u, %010u], %10lu nanoseconds per update, %5.3lf%% full\n", i + d, i, d, cfix_keys(h), cfix_min(h), cfix_max(h), (t2 - t1) / K, fill(h));
#ifdef STATS
			cfix_stats(h, &stats);
			fprintf(stderr, "HISTOGRAM: ");
			for (g = 0; g <= CFIX_BIN_SIZE; g++) fprintf(stderr, "%5.2f%% ", 100.0 * (float)stats.hist[g] / (float)cfix_bins(h));
			fprintf(stderr, "\nPRIMARY: %u %.4f\n", stats.primary, 100.0 * (float)stats.primary / (float)cfix_keys(h));
			fprintf(stderr, "\n\n");
#endif
			t1 = t2;
		}
	}

	t1 = nanoseconds();
	op = "deletion";
	for (key = k = 0; key < N; key++, k++) {
		if (get(key)) {
			assert(cfix_delete(h, KEY));
			++d;
			clr(key);
		}
		if (i - d != cfix_keys(h)) {
			fprintf(stderr, "Entry count mismatch: insertions = %lu, deletions = %lu, projected entries = %lu, actual entries = %u, last entry = 0x%08x, last operation = %s\n",
				   i, d, i - d, cfix_keys(h), KEY, op);	
			assert(0);
		}
		if ((k + 1) % K == 0) {
			t2 = nanoseconds();
			fprintf(stderr, "DELETE: %10lu updates, %10lu insertions, %10lu deletions, %10u entries in the range [%010u, %010u], %10lu nanoseconds per update, %5.3lf%% full\n", i + d, i, d, cfix_keys(h), cfix_min(h), cfix_max(h), (t2 - t1) / K, fill(h));
#ifdef STATS
			cfix_stats(h, &stats);
			fprintf(stderr, "HISTOGRAM: ");
			for (g = 0; g <= CFIX_BIN_SIZE; g++) fprintf(stderr, "%5.2f%% ", 100.0 * (float)stats.hist[g] / (float)cfix_bins(h));
			fprintf(stderr, "\nPRIMARY: %u %.4f\n", stats.primary, 100.0 * (float)stats.primary / (float)cfix_keys(h));
			fprintf(stderr, "\n\n");
#endif
			t1 = t2;
		}
	}
	fprintf(stderr, "%10lu updates, %10lu insertions, %10lu deletions, %10u entries\n", i + d, i, d, cfix_keys(h));

	cfix_destroy(&h);
	m2_report(buf, BUFSIZE);
	fprintf(stderr, "\n%s\n", buf);
	m2_exit();
	free(bit);
}
