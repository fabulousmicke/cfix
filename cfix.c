/**
 * @file cfix.c
 * @brief Hash table for 32-bit entries, i.e. (key, data) pairs.
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
#include <string.h>
#include <math.h>
#include "cfix.h"
#include "hash_primes.h"

//#define CFIX_CHECK

//#define CFIX_VERBOSE


#define CFIX_INFDATA

#define CFIX_BIN_LOCATE_BINARY

#define CFIX_TTL(h) ((h)->depth < (h)->bins ? (h)->depth : (h)->bins) 

#define CFIX_INF ((uint32_t)0xffffffff)

#define CFIX_KEY(h, base, offset) ((h)->bin[(base) * (h)->size][offset])

#define CFIX_DATA(h, base, offset) (&((h)->bin[(base) * (h)->size + 1][(offset) * ((h)->size - 1)]))

#define CFIX_NODATA 0xdeadbabe

static m2_t *cfix_handle = NULL,
			*cfix_bin_handle = NULL,
			*cfix_iter_handle = NULL;

typedef uint32_t cfix_bin_t[CFIX_BIN_SIZE];

static void cfix_data_clear(cfix_t *h, uint32_t base, uint32_t offset);

struct cfix {
	cfix_bin_t *bin;	/*< Array of bins corresponding to a cache lines. */
#ifdef CFIX_INFDATA
	uint32_t *infdata;	/*< Data associated with infinity, i.e. 0xffffffff. */
#endif
	uint64_t version;	/*< Running version number which is increased by one on each update operation. */
	uint32_t prix;		/*< Current prime index. */
	uint32_t bins;		/*< Current number of bins. */
	uint32_t keys;		/*< Current number of keys. */
	uint32_t size;		/*< Size each entry measured in number of uint32_t's. */
	uint32_t depth;		/*< Maximum recursive depth for cuckoo insertion - higher number yields more expensive insertion and higher fill factor. */
	uint32_t min;		/*< Smallest key present in the hash table. */
	uint32_t max;		/*< Largest key present in the hash table. */
	double lower;		/*< Lower fill threshold 0.0 - 1.0 but smaller than upper threshold. When fill ratio go below threshold after deletion the table is shrunk by reducing number of bins. */
	double upper;		/*< Upper fill threshold 0.0 - 1.0 but larger than upper threshold. When fill ratio will exceed threshold after insertion the table is grown before insertion. */  
	double growth;		/*< Base growth factor for increasing primes index and number of bins when insertion fails - controls level of growth in bin increase. */
	double attempt;		/*< Attempt factor when increasing prime index when bin increase fails - controls level of increase for next attempt when bin increase fails. */
	double random;		/*< Random factor used to compute prime index and bin increase - controls level of randomness in bin increase. */
#ifdef CFIX_INFDATA
	uint32_t _infdata[CFIX_DATA_MAXSIZE];	/*< Storage location for data associated with infinity, i.e. 0xffffffff. */
#endif
};

/** @brief Iterator data type. */
struct cfix_iter {
	cfix_t *h;			/*< CFIX instance associated with iterator. */
	uint64_t version;	/*< CFIX version immediately after iterator reset. */
	uint32_t base;		/*< Current base. */
	uint32_t offset;	/*< Current offset. */
};

/*****************************************************************************
 * Hash functions from http://burtleburtle.net/bob/hash/integer.html         *
 * Author: Bob Jenkins                                                       *
 *****************************************************************************/
	static uint32_t
cfix_full_avalanche(uint32_t a)
{
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

	static uint32_t
cfix_half_avalanche(uint32_t a)
{
	a = ~a;
	a = (a + 0x479ab41d) + (a << 8);
	a = (a ^ 0xe4aa10ce) ^ (a >> 5);
	a = (a + 0x9942f0a6) - (a << 14);
	a = (a ^ 0x5aedd67d) ^ (a >> 3);
	a = (a + 0x17bea992) + (a << 7);
	return a;
}
/*****************************************************************************/

	static cfix_t *
cfix_reuse(void)
{
	if (cfix_handle == NULL) {
		cfix_handle = m2_create("cfix_t", sizeof(cfix_t));
		assert(cfix_handle != NULL);
	}
	return (cfix_t *)m2_reuse(cfix_handle, 1, true);
}

	static void
cfix_recycle(cfix_t *k)
{
	m2_recycle(cfix_handle, (void *)k, 1);
}

	static cfix_bin_t *
cfix_bin_reuse(size_t n)
{
	if (cfix_bin_handle == NULL) {
		cfix_bin_handle = m2_create("cfix_bin_t", sizeof(cfix_bin_t));
		assert(cfix_bin_handle != NULL);
	}
	return (cfix_bin_t *)m2_reuse(cfix_bin_handle, n, false);
}

	static void
cfix_bin_recycle(cfix_bin_t *bin, size_t n)
{
	m2_recycle(cfix_bin_handle, bin, n);
}

	static cfix_iter_t *
cfix_iter_reuse(void)
{
	if (cfix_iter_handle == NULL) {
		cfix_iter_handle = m2_create("cfix_iter_t", sizeof(cfix_iter_t));
	}
	return (cfix_iter_t *)m2_reuse(cfix_iter_handle, 1, false);
}

	static void
cfix_iter_recycle(cfix_iter_t *iter)
{
	m2_recycle(cfix_iter_handle, (void *)iter, 1);
}

	uint32_t
cfix_keys(cfix_t *h)
{
	return h->keys;
}

	uint32_t
cfix_bins(cfix_t *h)
{
	return h->bins;
}

	uint32_t
cfix_min(cfix_t *h)
{
	return h->min;
}

	uint32_t
cfix_max(cfix_t *h)
{
	return h->max;
}

	static uint32_t
cfix_keys_to_prix(uint32_t keys)
{
	uint32_t result;

	for (result = 0; hash_primes_index_to_number(result) * CFIX_BIN_SIZE < keys; result++) {
	}

	return result;
}

	static void
cfix_bin_init(cfix_t *h)
{
	uint32_t b, o;

	for (b = 0; b < h->bins; b++) {
		for (o = 0; o < CFIX_BIN_SIZE; o++) {
			CFIX_KEY(h, b, o) = CFIX_INF;
			cfix_data_clear(h, b, o);
		}
	}
}

#ifdef CFIX_VERBOSE
	static void
cfix_bin_dump(cfix_t *h, uint32_t base)
{
	uint32_t offset;

	for  (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
		fprintf(stderr, "%010u ", CFIX_KEY(h, base, offset));
	}
	fprintf(stderr, "\n");
}
#endif

	static uint32_t
cfix_bin_count(cfix_t *h, uint32_t base)
{
	uint32_t result = 0, offset;

	for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
		if (CFIX_KEY(h, base, offset) == CFIX_INF) break;
		++result;
	}
	return result;
}

#ifdef CFIX_CHECK
	static bool
cfix_bin_check(cfix_t *h, uint32_t base)
{
	uint32_t o;

	for (o = 0; o < CFIX_BIN_SIZE - 1; o++) {
		uint32_t curr = CFIX_KEY(h, base, o),
				 next = CFIX_KEY(h, base, o + 1);

		if (curr < next) continue;
		if (curr > next) {
			fprintf(stderr, "\nFATAL ERROR in cfix_bin_check - keys out of order at base %u, offset %u\n", base, o);
			cfix_bin_dump(h, base);
			fprintf(stderr, "\n\n");
			return false;
		}
		if (curr != CFIX_INF) {
			fprintf(stderr, "\nFATAL ERROR in cfix_bin_check - dublicate keys at base %u, offset %u\n", base, o);
			cfix_bin_dump(h, base);
			fprintf(stderr, "\n\n");
			return false;
		}	
	}
	return true;
}
#endif

	static inline bool
cfix_bin_locate(
		cfix_t *h,
		uint32_t base,
		uint32_t key,
		uint32_t *offset)
{
#ifdef CFIX_BIN_LOCATE_LINEAR
#	error "Use binary!"
	uint32_t o;

	for (o = 0; o <  CFIX_BIN_SIZE; o++) {
		if (key == cfix_key(h, h->bin[base][o])) {
			(*offset) = o;
			return true;
		}
		if (key < cfix_key(h, h->bin[base][o])) return false;
	}
#elif defined(CFIX_BIN_LOCATE_BINARY)
	uint32_t *a0 = &CFIX_KEY(h, base, 0), *a;

	a = a0;
	a += (key >= a[8]) << 3;
	a += (key >= a[4]) << 2;
	a += (key >= a[2]) << 1;
	a += (key >= a[1]) << 0;
	if (*a == key) {
		*offset = (uint32_t)(a - a0);
		return true;
	}
	for (uint32_t o = 0; o < CFIX_BIN_SIZE; o++) assert(key != CFIX_KEY(h, base, o)); 
	return false;
#else
#	error "Bin locate search method not specified!"
#endif
	return false;
}

	void
cfix_create(
		cfix_t **h,
		cfix_config_t *conf)
{
	if (conf == NULL) {
		static cfix_config_t default_conf = {
			CFIX_CONFIG_DEFAULT_START,
			CFIX_CONFIG_DEFAULT_DATA,
			CFIX_CONFIG_DEFAULT_DEPTH,
			CFIX_CONFIG_DEFAULT_GROWTH,
			CFIX_CONFIG_DEFAULT_ATTEMPT,
			CFIX_CONFIG_DEFAULT_RANDOM
		};

		conf = &default_conf;
	}

	assert(0.0 <= conf->lower && conf->lower < conf->upper && conf->upper <= 1.0);
	assert(conf->data <= CFIX_DATA_MAXSIZE);

	assert(h != NULL);
	(*h) = cfix_reuse();

	(*h)->keys = 0;

	(*h)->prix = cfix_keys_to_prix(conf->start);
	(*h)->bins = hash_primes_index_to_number((*h)->prix);

	(*h)->size = conf->data + 1;

	(*h)->bin = cfix_bin_reuse(((*h)->bins * (*h)->size));
	cfix_bin_init(*h);

	(*h)->version = 0l;

	(*h)->depth = conf->depth;
	(*h)->lower = conf->lower;
	(*h)->upper = conf->upper;
	(*h)->growth = conf->growth;
	(*h)->attempt = conf->attempt;
	(*h)->random = conf->random;

	(*h)->min = CFIX_INF;
	(*h)->max = 0;
#ifdef CFIX_INFDATA
	(*h)->infdata = NULL;
#endif
}

	void
cfix_destroy(cfix_t **h)
{
	cfix_bin_recycle((*h)->bin, (*h)->bins * (*h)->size);
	cfix_recycle((*h));
	(*h) = NULL;
}

	cfix_t *
cfix_clone(cfix_t *h)
{
	cfix_t *result;

	result = cfix_reuse();

	memcpy(result, h, sizeof(cfix_t));

	result->bin = cfix_bin_reuse(result->bins * result->size);

	memcpy(result->bin, h->bin, result->bins * result->size * CFIX_BIN_SIZE * sizeof(uint32_t));

#ifdef CFIX_INFDATA
	if (result->infdata != NULL) {
		result->infdata = result->_infdata;
		memcpy(result->infdata, h->infdata, (h->size - 1) * sizeof(uint32_t));
	}
#endif

	return result;
}

	static inline bool
cfix_locate(
		cfix_t *h,
		uint32_t key,
		uint32_t *base,
		uint32_t *offset,
		uint32_t **data)
{
	(*base) = cfix_full_avalanche(key) % h->bins;
	if (cfix_bin_locate(h, (*base), key, offset)) {
		(*data) = CFIX_DATA(h, *base, *offset);
		return true;
	}
	(*base) = cfix_half_avalanche(key) % h->bins;
	if (cfix_bin_locate(h, (*base), key, offset)) {
		(*data) = CFIX_DATA(h, *base, *offset);
		return true;
	}
	return false;
}
	static void
cfix_entry_move(
		cfix_t *h,
		uint32_t src_base,
		uint32_t src_offset,
		uint32_t dst_base,
		uint32_t dst_offset)
{
	CFIX_KEY(h, dst_base, dst_offset) = CFIX_KEY(h, src_base, src_offset);
	if (h->size == 1) return;
	memcpy(CFIX_DATA(h, dst_base, dst_offset), CFIX_DATA(h, src_base, src_offset), (h->size - 1) * sizeof(uint32_t));
}

	static void
cfix_entry_copy(
		cfix_t *h,
		uint32_t src_base,
		uint32_t src_offset,
		uint32_t *dst_entry)
{
	assert(dst_entry != NULL);
	(*dst_entry) = CFIX_KEY(h, src_base, src_offset);
	if (h->size == 1) return;
	memcpy(dst_entry + 1, CFIX_DATA(h, src_base, src_offset), (h->size - 1) * sizeof(uint32_t));
}

	static void
cfix_entry_paste(
		cfix_t *h,
		uint32_t *src_entry,
		uint32_t dst_base,
		uint32_t dst_offset)
{
	assert(src_entry != NULL);
	CFIX_KEY(h, dst_base, dst_offset) = (*src_entry);
	if (h->size == 1) return;
	memcpy(CFIX_DATA(h, dst_base, dst_offset), src_entry + 1, (h->size - 1) * sizeof(uint32_t));
}

	static void
cfix_data_store(
		cfix_t *h,
		uint32_t *src_data,
		uint32_t dst_base,
		uint32_t dst_offset)
{
	if (h->size == 1) return;
	assert(src_data != NULL);
	memcpy(CFIX_DATA(h, dst_base, dst_offset), src_data, (h->size - 1) * sizeof(uint32_t));
}

	static void
cfix_data_retrieve(
		cfix_t *h,
		uint32_t src_base,
		uint32_t src_offset,
		uint32_t *dst_data)
{
	if (h->size == 1) return;
	assert(dst_data != NULL);
	memcpy(dst_data, CFIX_DATA(h, src_base, src_offset), (h->size - 1) * sizeof(uint32_t));
}

	static void
cfix_data_clear(
		cfix_t *h,
		uint32_t base,
		uint32_t offset)
{
	uint32_t i, *d;

	if (h->size == 1) return;
	d = CFIX_DATA(h, base, offset);
	for (i = 0; i < h->size - 1; i++) d[i] = CFIX_NODATA;
}

	static bool
cfix_data_empty(
		cfix_t *h,
		uint32_t base,
		uint32_t offset)
{
	uint32_t i, *d;

	if (h->size == 1) return true;
	d = CFIX_DATA(h, base, offset);
	for (i = 0; i < h->size - 1; i++) if (d[i] != CFIX_NODATA) return false;
	return true;
}

	static void
cfix_roll_left(
		cfix_t *h,
		uint32_t base,
		uint32_t offset)
{
	uint32_t key = CFIX_KEY(h, base, offset),
			 o,
			 entry[CFIX_DATA_MAXSIZE + 1];

	cfix_entry_copy(h, base, offset, entry);

	for (o = offset; o > 0; o--) {
		if (CFIX_KEY(h, base, o - 1) < key) break;
		cfix_entry_move(h, base, o - 1, base, o);
		cfix_entry_paste(h, entry, base, o - 1); 
	}
#ifdef CFIX_CHECK
	assert(cfix_bin_check(h, base));
#endif
}

	static void
cfix_roll_right(
		cfix_t *h,
		uint32_t base,
		uint32_t offset)
{
	uint32_t key = CFIX_KEY(h, base, offset),
			 o,
			 entry[CFIX_DATA_MAXSIZE + 1];

	cfix_entry_copy(h, base, offset, entry);

	for (o = offset; o < CFIX_BIN_SIZE - 1; o++) {
		if (CFIX_KEY(h, base, o + 1) > key) break;
		cfix_entry_move(h, base, o + 1, base, o);
		cfix_entry_paste(h, entry, base, o + 1);
	}

#ifdef CFIX_CHECK
	assert(cfix_bin_check(h, base));
#endif
}

	static void
cfix_adjust(
		cfix_t *h,
		uint32_t base,
		uint32_t *offset)
{
	uint32_t entry[CFIX_DATA_MAXSIZE + 1];

	for (;;) {
		uint32_t new_offset;
		bool l, r;

		if ((*offset) == 0) {
			l = true;
		} else {
			l = CFIX_KEY(h, base, (*offset) - 1) < CFIX_KEY(h, base, (*offset));
			assert(CFIX_KEY(h, base, (*offset) - 1) != CFIX_KEY(h, base, (*offset)));
		}
		if ((*offset) == CFIX_BIN_SIZE - 1) {
			r = true;
		} else {
			r = CFIX_KEY(h, base, (*offset)) < CFIX_KEY(h, base, (*offset) + 1);
			assert(CFIX_KEY(h, base, (*offset)) != CFIX_KEY(h, base, (*offset) + 1));
		}
		if (l && r) {
			return;
		}
		if (!l) {
			new_offset = (*offset) - 1;
		} else {
			assert(!r); 
			new_offset = (*offset) + 1;
		}
		cfix_entry_copy(h, base, *offset, entry);
		cfix_entry_move(h, base, new_offset, base, *offset);
		cfix_entry_paste(h, entry, base, new_offset);
		(*offset) = new_offset;
	}
}

	static bool
cfix_cuckoo(
		cfix_t *h,
		uint32_t key,
		uint32_t *data,
		uint32_t ttl)
{
	uint32_t base_full, base_half, offset, cand_offset, cand_key, *cand_data, cand_entry[CFIX_DATA_MAXSIZE + 1];

	if (ttl == 0) {
		/* Maximum recursive depth reached. */
		return false;
	}

	/*
	 * Trying to insert in primary block.
	 */

	base_full = cfix_full_avalanche(key) % h->bins;
#ifdef CFIX_CHECK
	assert(cfix_bin_check(h, base_full));
#endif
	if (CFIX_KEY(h, base_full, CFIX_BIN_SIZE - 1) == CFIX_INF) {
		assert(cfix_data_empty(h, base_full, CFIX_BIN_SIZE - 1));
		CFIX_KEY(h, base_full, CFIX_BIN_SIZE - 1) = key;
		cfix_data_store(h, data, base_full, CFIX_BIN_SIZE - 1);
		cfix_roll_left(h, base_full, CFIX_BIN_SIZE - 1);
		return true;
	}

	/*
	 * Primary block full - try secondary block.
	 */

	base_half = cfix_half_avalanche(key) % h->bins;
#ifdef CFIX_CHECK
	assert(cfix_bin_check(h, base_half));
#endif
	if (CFIX_KEY(h, base_half, CFIX_BIN_SIZE - 1) == CFIX_INF) {
		assert(cfix_data_empty(h, base_half, CFIX_BIN_SIZE - 1));
		CFIX_KEY(h, base_half, CFIX_BIN_SIZE - 1) = key;
		cfix_data_store(h, data, base_half, CFIX_BIN_SIZE - 1);
		cfix_roll_left(h, base_half, CFIX_BIN_SIZE - 1);
		return true;
	}

	/*
	 * Secondary block full - locate candidate in primary block to move.
	 */

	for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
		cand_key = CFIX_KEY(h, base_full, offset);

		if (base_full == cfix_full_avalanche(cand_key) % h->bins) {
			/* Primary block is also primary block for candidate. */
			cfix_entry_copy(h, base_full, offset, cand_entry);
			cand_data = cand_entry + 1;
			cand_offset = offset;

			CFIX_KEY(h, base_full, cand_offset) = key;
			cfix_data_store(h, data, base_full, cand_offset);
			cfix_adjust(h, base_full, &cand_offset);

			if (cfix_cuckoo(h, cand_key, cand_data, ttl - 1)) {
				return true;
			}

			/* Recursive move of candidate failed - restore and move on. */
			assert(CFIX_KEY(h, base_full, cand_offset) == key);
			cfix_entry_paste(h, cand_entry, base_full, cand_offset);
			cfix_adjust(h, base_full, &cand_offset);
			assert(cand_offset == offset);
		}
	}

	/*
	 * Failed to locate candidate in primary block, trying secondary.
	 */

	for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
		cand_key = CFIX_KEY(h, base_half, offset);

		if (base_half == cfix_full_avalanche(cand_key) % h->bins) {
			/* Primary block is also primary block for candidate. */
			cfix_entry_copy(h, base_half, offset, cand_entry);
			cand_data = cand_entry + 1;
			cand_offset = offset;

			CFIX_KEY(h, base_half, cand_offset) = key;
			cfix_data_store(h, data, base_half, cand_offset);
			cfix_adjust(h, base_half, &cand_offset);

			if (cfix_cuckoo(h, cand_key, cand_data, ttl - 1)) {
				return true;
			}

			/* Recursive move of candidate failed - restore and move on. */
			assert(CFIX_KEY(h, base_half, cand_offset) == key);
			cfix_entry_paste(h, cand_entry, base_half, cand_offset);
			cfix_adjust(h, base_half, &cand_offset);
			assert(cand_offset == offset);
		}
	}

	/*
	 * Insertion failed!
	 */
	return false;
}

	bool
cfix_insert(cfix_t *h, uint32_t key, uint32_t *data)
{
	cfix_t old;
	double factor;
	uint32_t base, offset, attempt, *old_data;

#ifdef CFIX_INFDATA
	if (key == CFIX_INF) {
		if (h->infdata == NULL) {
			h->infdata = h->_infdata;
			memcpy(h->infdata, data, (h->size - 1) * sizeof(uint32_t));
			++h->keys;
			return true;
		} else {
			return false;
		}
	}
#else
	assert(key < CFIX_INF);
#endif

	if (cfix_locate(h, key, &base, &offset, &old_data)) return false;

	if ((double)(h->keys + 1) / ((double)h->bins * (double)CFIX_BIN_SIZE) > h->upper) {
		/*
		 * Consider as failed insertion and increase table size.
		 */
#ifdef CFIX_VERBOSE
		fprintf(stderr, "FILLED:\n");
#endif
	} else if (cfix_cuckoo(h, key, data, CFIX_TTL(h))) {
		/* Insertion successful. */
		if (h->keys == 0 || key < h->min) h->min = key;
		if (h->keys == 0 || key > h->max) h->max = key;
		++h->keys;
		++h->version;
		return true;
	} else {
#ifdef CFIX_VERBOSE
		fprintf(stderr, "FAILED:\n");
#endif
	}

	/* Insertion failed - extend hash table. */
	attempt = 1;
	memcpy(&old, h, sizeof(cfix_t));

	for (;;) {
		factor =
			h->growth +
			h->attempt * attempt +
			h->random * drand48();
		h->prix = (uint32_t)((double)old.prix * factor);
		if (h->prix < old.prix + attempt) h->prix = old.prix + attempt;

		h->bins = hash_primes_index_to_number(h->prix);

#ifdef CFIX_VERBOSE
		fprintf(stderr, "GROWTH: %u -> %u\n", old.bins, h->bins);
#endif

		h->keys = 0;
#ifdef CFIX_INFDATA
		if (h->infdata != NULL) ++h->keys;
#endif
		h->min = CFIX_INF;
		h->max = 0;
		h->bin = cfix_bin_reuse(h->bins * h->size);
		cfix_bin_init(h);

		assert(cfix_cuckoo(h, key, data, CFIX_TTL(h)));
		h->min = h->max = key;
		h->keys++;

		for (base = 0; base < old.bins; base++) {
			for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
				uint32_t k = CFIX_KEY(&old, base, offset);

				if (k == CFIX_INF) break;

				if (cfix_cuckoo(h, k, CFIX_DATA(&old, base, offset), CFIX_TTL(h))) {
					if (h->keys == 0 || k < h->min) h->min = k;
					if (h->keys == 0 || k > h->max) h->max = k;
					++h->keys;
					continue;
				}
				/*
				 * Insertion failed despite extension - scrap and retry.
				 */
				goto retry;
			}
		}
		cfix_bin_recycle(old.bin, old.bins * old.size);
		++h->version;
		return true;
retry:
		cfix_bin_recycle(h->bin, h->bins * h->size);
		++attempt;
	}

	/* *** NOT REACHED *** */
	assert(0);
	return false;
}

	static bool
cfix_shrinkable(cfix_t *h)
{
	double fill = (double)h->keys / ((double)h->bins * (double)CFIX_BIN_SIZE);

	if (h->keys <= CFIX_BIN_SIZE) return false;

	return fill < h->lower;
}

	bool
cfix_delete(cfix_t *h, uint32_t key)
{
	uint32_t base, offset, *data;

#ifdef CFIX_INFDATA
	if (key == CFIX_INF) {
		if (h->infdata == NULL) {
			return false;
		} else {
			h->infdata = NULL;
			--h->keys;
			return true;
		}
	}
#else
	assert(key < CFIX_INF);
#endif

	if (!cfix_locate(h, key, &base, &offset, &data)) return false;

	CFIX_KEY(h, base, offset) = CFIX_INF;
	cfix_data_clear(h, base, offset);
	cfix_roll_right(h, base, offset);
	--h->keys;
	++h->version;
	if (h->keys == 0) h->min = h->max = CFIX_INF;

	if (cfix_shrinkable(h)) {
		/*
		 * Decrease size of table.
		 */
		cfix_t old;
		uint32_t attempt = 1, shrink_prix, shrink_keys;

		shrink_keys = (uint32_t)(((h->upper + h->lower) / 2) * (double)h->bins * (double)CFIX_BIN_SIZE);
		for (shrink_prix = h->prix; shrink_keys < hash_primes_index_to_number(shrink_prix) * CFIX_BIN_SIZE; shrink_prix--);

		memcpy(&old, h, sizeof(cfix_t));

		for (;;) {
			h->prix = (uint32_t)((double)shrink_prix + (double)(attempt - 1));
			if (h->prix < shrink_prix + attempt) h->prix = shrink_prix + attempt;

			assert(h->prix < old.prix);
			h->bins = hash_primes_index_to_number(h->prix);

			h->keys = 0;
#ifdef CFIX_INFDATA
		if (h->infdata != NULL) ++h->keys;
#endif
			h->min = CFIX_INF;
			h->max = 0;
			h->bin = cfix_bin_reuse(h->bins * h->size);
			cfix_bin_init(h);

			for (base = 0; base < old.bins; base++) {
				for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
					uint32_t k = CFIX_KEY(&old, base, offset);

					if (k == CFIX_INF) break;

					if (cfix_cuckoo(h, k, CFIX_DATA(&old, base, offset), CFIX_TTL(h))) {
						if (h->keys == 0 || k < h->min) h->min = k;
						if (h->keys == 0 || k > h->max) h->max = k;
						++h->keys;
						continue;
					}
					/*
					 * Insertion failed - scrap and retry.
					 */
					goto retry;
				}
			}

			cfix_bin_recycle(old.bin, old.bins * old.size);
			return true;
retry:
			cfix_bin_recycle(h->bin, h->bins * h->size);
			++attempt;
		}
	}

	return true;
}

	void
cfix_rebuild(cfix_t *h, double ratio)
{
	cfix_t old;
	uint32_t prix, keys, base, offset;

	assert((CFIX_RATIO_MIN <= ratio) && (ratio <= (double)1.0));

	memcpy(&old, h, sizeof(cfix_t));

	keys = (uint32_t)((double)h->keys / ratio);
	prix = cfix_keys_to_prix(keys);

	for (;;) {
		h->prix = prix;
		h->bins = hash_primes_index_to_number(h->prix);

#ifdef CFIX_VERBOSE
		fprintf(stderr, "COMPRESS: prix = %u, bins = %u, ratio %.2f%% ", h->prix, h->bins, 100.0 * (float)old.keys / (float)(h->bins * CFIX_BIN_SIZE));
#endif
		h->keys = 0;
#ifdef CFIX_INFDATA
		if (h->infdata != NULL) ++h->keys;
#endif
		h->min = CFIX_INF;
		h->max = 0;
		h->bin = cfix_bin_reuse(h->bins * h->size);
		cfix_bin_init(h);

		for (base = 0; base < old.bins; base++) {
			for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
				uint32_t k = CFIX_KEY(&old, base, offset);

				if (k == CFIX_INF) break;

				if (cfix_cuckoo(h, k, CFIX_DATA(&old, base, offset), CFIX_TTL(h))) {
					if (h->keys == 0 || k < h->min) h->min = k;
					if (h->keys == 0 || k > h->max) h->max = k;
					++h->keys;
					continue;
				}
				/*
				 * Insertion failed - scrap and retry.
				 */
				goto retry;
			}
		}
		cfix_bin_recycle(old.bin, old.bins * old.size);
		++h->version;
#ifdef CFIX_VERBOSE
		fprintf(stderr, "SUCCESS\n");
#endif
		return;
retry:
		cfix_bin_recycle(h->bin, h->bins * h->size);
#ifdef CFIX_VERBOSE
		fprintf(stderr, "FAILURE\n");
#endif
		++prix;
	}

	/* *** NOT REACHED *** */
	assert(0);
}

	bool
cfix_lookup(cfix_t *h, uint32_t key, uint32_t *data)
{
	uint32_t base, offset, *__unused;

#ifdef CFIX_INFDATA
	if (key == CFIX_INF) {
		if (h->infdata == NULL) {
			return false;
		} else {
			memcpy(data, h->infdata, (h->size - 1) * sizeof(uint32_t));
			return true;
		}
	}
#else
	assert(key < CFIX_INF);
#endif

	if (!cfix_locate(h, key, &base, &offset, &__unused)) return false;
	cfix_data_retrieve(h, base, offset, data);
	return true;
}

	bool
cfix_update(cfix_t *h, uint32_t key, uint32_t *data)
{
	uint32_t base, offset, *__unused;

#ifdef CFIX_INFDATA
	if (key == CFIX_INF) {
		if (h->infdata == NULL) {
			return false;
		} else {
			memcpy(h->infdata, data, (h->size - 1) * sizeof(uint32_t));
			return true;
		}
	}
#else
	assert(key < CFIX_INF);
#endif

	if (!cfix_locate(h, key, &base, &offset, &__unused)) return false;
	cfix_data_store(h, data, base, offset);
	++h->version;
	return true;
}

	void
cfix_apply(
		cfix_t *h,
		void(*fun)(uint32_t, uint32_t *, void *),
		void *aux)
{
	uint64_t version;
	uint32_t base, offset;

	version = h->version;
	for (base = 0; base < h->bins; base++) {
		for (offset = 0; offset < CFIX_BIN_SIZE; offset++) {
			uint32_t key = CFIX_KEY(h, base, offset);

			if (key	== CFIX_INF) break;
			fun(key, CFIX_DATA(h, base, offset), aux);
			if (version != h->version) {
				fprintf(stderr, "\n\nFATAL ERROR in \"cfix_apply\" - function call compromised CFIX instance!\n");
				exit(1);
			}
		}
	}
#ifdef CFIX_INFDATA
	if (h->infdata != NULL) {
		fun(CFIX_INF, h->infdata, aux);
	}
#endif
}

	void
cfix_iter_create(
		cfix_t *h,
		cfix_iter_t **iter)
{
	(*iter) = cfix_iter_reuse();
	assert((*iter) != NULL);

	(*iter)->h = h;
	cfix_iter_reset(h, (*iter));
}

	void
cfix_iter_destroy(cfix_iter_t **iter)
{
	cfix_iter_recycle((*iter));
	(*iter) = NULL;
}

	void
cfix_iter_reset(
		cfix_t *h,
		cfix_iter_t *iter)
{
	assert(h == iter->h);

	iter->version = h->version;
	iter->base = iter->offset = 0;
	if (h->keys == 0) return;
	if (CFIX_KEY(h, iter->base, iter->offset) != CFIX_INF) return;
	assert(cfix_iter_forward(h, iter) == CFIX_ITER_SUCCESS);
}

	cfix_iter_status_t
cfix_iter_current(
		cfix_t *h,
		cfix_iter_t *iter,
		uint32_t *key,
		uint32_t *data)
{
	assert(h == iter->h);

	if (iter->version != h->version) return CFIX_ITER_INVALID;

#ifdef CFIX_INFDATA
	if (iter->base == h->bins) {
		if (iter->offset == 0 && h->infdata != NULL) {
			(*key) = CFIX_INF;
			memcpy(data, h->infdata, (h->size - 1) * sizeof(uint32_t));
			return CFIX_ITER_SUCCESS;
		}
		return CFIX_ITER_FAILURE;
	}
#endif

	assert(iter->base < h->bins);
	assert(iter->offset < CFIX_BIN_SIZE);
	assert(CFIX_KEY(h, iter->base, iter->offset) != CFIX_INF);

	(*key) = CFIX_KEY(h, iter->base, iter->offset);
	cfix_data_retrieve(h, iter->base, iter->offset, data);

	return CFIX_ITER_SUCCESS;
}

	cfix_iter_status_t
cfix_iter_forward(
		cfix_t *h,
		cfix_iter_t *iter)
{
	assert(h == iter->h);

	if (iter->version != h->version) return CFIX_ITER_INVALID;

	++iter->offset;
	if (iter->offset == CFIX_BIN_SIZE) {
		iter->offset = 0;
		++iter->base;
	}

	for (; iter->base < h->bins; iter->base++, iter->offset = 0) {
		//if (iter->offset == CFIX_BIN_SIZE) continue;
		assert(iter->offset <= CFIX_BIN_SIZE);
		if (CFIX_KEY(h, iter->base, iter->offset) == CFIX_INF) continue;
		break;
	}
#ifdef CFIX_INFDATA
	if (iter->base == h->bins) {
		if (iter->offset == 0 && h->infdata != NULL) {
			return CFIX_ITER_SUCCESS;
		}
		return CFIX_ITER_FAILURE;
	}
#else
	if (iter->base == h->bins) return CFIX_ITER_FAILURE;
#endif
	return CFIX_ITER_SUCCESS;	
}

	void
cfix_stats(cfix_t *h, cfix_stats_t *stats)
{
	static uint32_t i, b, o;

	stats->primary = 0;
	for (i = 0; i < CFIX_BIN_SIZE + 1; i++) stats->hist[i] = 0;

	for (b = 0; b < h->bins; b++) {
		++stats->hist[cfix_bin_count(h, b)];
		for (o = 0; o < CFIX_BIN_SIZE; o++) {
			uint32_t key = CFIX_KEY(h, b, o);

			if (key == CFIX_INF) break;
			if (b == (cfix_full_avalanche(key) % h->bins)) ++stats->primary;
		}
	}
}
