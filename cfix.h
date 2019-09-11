/**
 * @file cfix.h
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
#ifndef CFIX
#define CFIX

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "m2.h"

/** @brief Size of each key (i.e. key + data) in bytes. */
#define CFIX_KEY_SIZE sizeof(uint32_t)

/** @brief Number of entries per bin. */
#define CFIX_BIN_SIZE (M2_ALIGNMENT / CFIX_KEY_SIZE)

/** @brief Maximum data size measured in number of uint32_t's. */
#define CFIX_DATA_MAXSIZE 15

/** @brief Minimum compression ratio supported. */
#define CFIX_RATIO_MIN ((double)0.01)

/** @brief Configuration default values. If you don't know what you are doing, use these (in particular the last three :). */
#define CFIX_CONFIG_DEFAULT_START		112
#define CFIX_CONFIG_DEFAULT_DATA		1
#define CFIX_CONFIG_DEFAULT_DEPTH		3
#define CFIX_CONFIG_DEFAULT_LOWER		0.0
#define CFIX_CONFIG_DEFAULT_UPPER		1.0
#define CFIX_CONFIG_DEFAULT_GROWTH	1.5
#define CFIX_CONFIG_DEFAULT_ATTEMPT	0.5
#define CFIX_CONFIG_DEFAULT_RANDOM	0.5

/**
 * @brief CFIX abstract data type.
 */
struct cfix;
typedef struct cfix cfix_t;

/**
 * @brief CFIX config data type - for default values see CFIX_CONFIG_DEFAULT_* above.
 */
struct cfix_config {
	uint32_t start;	/*< Target number of keys to start with (initially dimensioned for this). */
	uint32_t data;	/*< Number of uint32_t's per entry used for data representation. */
	uint32_t depth;	/*< Maximum recursive depth for cuckoo insertion - higher number yields more expensive insertion and higher fill factor. */
	double lower;	/*< Lower fill threshold 0.0 - 1.0 but smaller than upper threshold. When fill ratio go below threshold after deletion the table is shrunk by reducing number of bins. */
	double upper;	/*< Upper fill threshold 0.0 - 1.0 but larger than upper threshold. When fill ratio will exceed threshold after insertion the table is grown before insertion. */  
	double growth;	/*< Base growth factor for increasing primes index and number of bins when insertion fails - controls level of expansion in bin increase. */
	double attempt;	/*< Attempt factor when increasing prime index when bin increase fails - controls level of increase for next attempt when bin increase fails. */
	double random;	/*< Random factor used to compute prime index and bin increase - controls level of randomness in bin increase. */
};
typedef struct cfix_config cfix_config_t;

/**
 * @brief CFIX statistics data type used to collect statistics not available by other means.
 */
struct cfix_stats {
	uint32_t hist[CFIX_BIN_SIZE + 1];	/*< Histogram of number of bins and number of keys in bin. */
	uint32_t primary;					/*< Number of keys stored in primary location. */ 
};
typedef struct cfix_stats cfix_stats_t;

/**
 * @brief CFIX iterator data type.
 */
struct cfix_iter;
typedef struct cfix_iter cfix_iter_t;

/**
 * @brief CFIX iterator status data type.
 */
typedef enum {
	CFIX_ITER_SUCCESS,	/*< Returned on successful retrieval of current (key, data) pair and successful move to the next item. */
	CFIX_ITER_FAILURE,	/*< Returned if retrieval of current (key, data) pair fails (i.e. empty) or when move to the next item fails due to the current item being the last item. */ 
	CFIX_ITER_INVALID	/*< Returned when an update operation has caused the iterator to become obsolete. */
} cfix_iter_status_t;

/**
 * @brief Create new hash table instance.
 *
 * @param h Location to store new CFIX instance.
 * @param conf Hash table instance configuration or NULL for default configuration.
 *
 * @note 32 - keysize bits represent the data associated with each key.
 */
void cfix_create(cfix_t **h, cfix_config_t *conf);

/**
 * @brief Destroy existing CFIX instance.
 *
 * @param h Location of CFIX instance to destroy.
 */
void cfix_destroy(cfix_t **h);

/**
 * @brief Clone CFIX instance.
 *
 * @param h CFIX instance to clone.
 * 
 * @return Cloned CFIX instance.
 */ 
cfix_t *cfix_clone(cfix_t *h);

/**
 * @brief Insert (key, data) pair in CFIX instance.
 *
 * @param h CFIX instance to perform insertion in.
 * @param key Key to insert - key must be smaller than cfix_key_lim.
 * @param data Location of data to insert.
 *
 * @return Boolean true on success and false otherwise (e.g. key present).
 *
 * @note If CFIX instance data size (see cfix_conf_t and cfix_create) data is ignored.
 */
bool cfix_insert(cfix_t *h, uint32_t key, uint32_t *data);

/**
 * @brief Delete key and associated data from CFIX instance.
 *
 * @param h CFIX instance to perform deletion from.
 * @param key Key to delete - key must be smaller than cfix_key_lim.
 *
 * @return Boolean true on success and false otherwise (e.g. key missing).
 */
bool cfix_delete(cfix_t *h, uint32_t key);

/**
 * @brief Lookup data associated with key in CFIX instance.
 *
 * @param h CFIX instance to perform lookup in.
 * @param key Key to lookup - key must be smaller than cfix_key_lim.
 * @param data Location where looked up data is stored on successful lookup.
 *
 * @return Boolean true on success and false otherwise (e.g. key missing).
 *
 * @note If CFIX instance data size (see cfix_conf_t and cfix_create) data is ignored.
 */
bool cfix_lookup(cfix_t *h, uint32_t key, uint32_t *data);

/**
 * @brief Update data associated with key in CFIX instance.
 *
 * @param h CFIX instance to perform update in.
 * @param key Key associate with data key must be smaller than cfix_key_lim.
 * @param data Data to associate with key must be smaller than cfix_data_lim.
 *
 * @return Boolean true on success and false otherwise (e.g. key missing).
 *
 * @note Update is equivalent to, but more efficient than, delete plus insert.
 * @note It does not make sense to call update is data size is zero.
 */
bool cfix_update(cfix_t *h, uint32_t key, uint32_t *data);

/**
 * @brief Return the smallest key either present or that has been present, since the last reconstruction (see cfix_rebuild), in a CFIX instance.
 *
 * @param h CFIX instance.
 *
 * @return The smallest key either present or that has been present, since the most recent reconstruction, in the CFIX instance..
 */
uint32_t cfix_min(cfix_t *h);

/**
 * @brief Return the largest key either present or that has been present, since the last reconstruction (see cfix_rebuild), in a CFIX instance.
 *
 * @param h CFIX instance.
 *
 * @return The largest key either present or that has been present, since the most recent reconstruction, in the CFIX instance..
 */
uint32_t cfix_max(cfix_t *h);

/**
 * @brief Return current number of keys in the hash table.
 *
 * @param h CFIX instance.
 *
 * @return Current number of keys in the table.
 *
 */
uint32_t cfix_keys(cfix_t *h);

/**
 * @brief Return current number of bins.
 *
 * @param h CFIX instance.
 *
 * @return Current number of bins.
 *
 * @note Number of slots obtained by multiplying with CFIX_BIN_SIZE, fill factor = keys / slots.
 */
uint32_t cfix_bins(cfix_t *h);

/**
 * @brief Rebuild the hash table w r t a target fill ratio.
 *
 * @param h CFIX instance.
 * @param ratio Target fill ratio in the range CFIX_RATIO_MIN to 1.0.
 *
 * @note High target fill ratio (e.g. 1.0) guarantees that the minimum amount of bins that can represent the present set of keys is used at the cost of additional iterations whereas low ratio is fast and yields a high percentage of keys available for lookup in a single memory access (can be checked using cfix_stats).  
 */
void cfix_rebuild(cfix_t *h, double ratio);

/**
 * @brief Generate statistics.
 *
 * @param h CFIX instance.
 */
void cfix_stats(cfix_t *h, cfix_stats_t *stats);

/**
 * @brief Apply call-back function on all (key, data)-pairs in CFIX instance.
 *
 * @param h CFIX instance.
 * @param fun Call-back function where the arguments are key, data and auxiliary pointer in that order.
 * @param aux Auxiliary pointer passed to call-back function.
 */
void cfix_apply(cfix_t *h, void(*fun)(uint32_t, uint32_t *, void *), void *aux);

/**
 * @brief Create and reset iterator.
 *
 * @param h CFIX instance.
 * @param iter Location of iterator to be created.
 */
void cfix_iter_create(cfix_t *h, cfix_iter_t **iter);

/**
 * @brief Destroy iterator.
 *
 * @param iter Location of iterator to be destroyed.
 */
void cfix_iter_destroy(cfix_iter_t **iter);

/**
 * @brief Reset iterator.
 *
 * @param h CFIX instance.
 * @param iter Iterator.
 */
void cfix_iter_reset(cfix_t *h, cfix_iter_t *iter);

/**
 * @brief Retrieve current (key, data)-pair.
 *
 * @param h CFIX instance.
 * @param iter Iterator.
 * @param key Location to store retrieved key.
 * @param data Location to store retrieved data.
 *
 * @return CFIX_ITER_SUCCESS on successful retrieval, CFIX_ITER_FAILURE if retrieval fails due to empty table, and CFIX_ITER_INVALID if the iterator is obsolete as a result from an update operation.
 */
cfix_iter_status_t cfix_iter_current(cfix_t *h, cfix_iter_t *iter, uint32_t *key, uint32_t *data);

/**
 * @brief Move iterator to the next (key, data) pair if possible.
 *
 * @param h CFIX instance.
 * @param iter Iterator.
 * 
 * @return CFIX_ITER_SUCCESS on success, CFIX_ITER_FAULURE if current item is the last iten, and CFIX_ITER_INVALID if the iterator is obsolete as a result from an update operation.
 */
cfix_iter_status_t cfix_iter_forward(cfix_t *h, cfix_iter_t *iter);

#if 0
/**
 * @brief Return maximum key value.
 *
 * @param h CFIX instance.
 *
 * @return Maximum key value (as determined by configuration when instance was created).
 */
uint32_t cfix_key_max(cfix_t *h);

/**
 * @brief Return maximum data value.
 *
 * @param h CFIX instance.
 *
 * @return Maximum data value (as determined by configuration when instance was created).
 */
uint32_t cfix_data_max(cfix_t *h);
#endif

#endif /* CFIX */
