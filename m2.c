/**
 * @file m2.c
 * @brief Memory manager = mm = m^2 implementation.
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
#include <string.h>

#include "m2.h"

#define M2_ERROR_BUFSIZE 1024

#define M2_REPORT_BUFSIZE 65536

//#define M2_RECYCLED_BLOCKSIZE 256
#define M2_RECYCLED_BLOCKSIZE 0

#define M2_REPORT_INTERVAL 0

#define M2_LINK(p) (*((void **)p))

struct m2 {
	m2_t *link;
	size_t size;
	uint64_t reused;
	uint64_t recycled;
	uint64_t newusage;
	uint64_t oldusage;
	uint64_t maxusage;
	char id[M2_IDSIZE];
};

static m2_t m2_total = {NULL, 0, 0, 0, 0, 0, 0, "total"};

static m2_t *m2_anchor = NULL;

static void *m2_recycled_block[M2_RECYCLED_BLOCKSIZE];

static bool m2_initialized = false;
static void (*m2_error_fun)(char *) = NULL;

static char m2_report_buf[M2_REPORT_BUFSIZE];

static void
m2_default_error_fun(char *msg)
{
	fprintf(stderr, "\n\n%s\n", msg);
}

static void
m2_error(char *msg)
{
	m2_error_fun(msg);
}

static void
m2_abort(char *msg)
{
	m2_error(msg);
	exit(1);
}

void
m2_init(void (*error)(char *))
{
		int i;

		for (i = 0; i < M2_RECYCLED_BLOCKSIZE; i++) {
			m2_recycled_block[i] = (void *)NULL;
		}

		m2_initialized = true;
	
		if (error == NULL) {
			m2_error_fun = m2_default_error_fun;
		} else {
			m2_error_fun = error;
		}
}

void
m2_exit(void)
{
	m2_t *cur;

	for (cur = m2_anchor; cur != NULL; cur = cur->link) {
		if (cur->reused != cur->recycled) {
			m2_error("\n\nFATAL ERROR in m2_exit - all items must be recycled before exiting!\n");  
			m2_abort(m2_report(m2_report_buf, M2_REPORT_BUFSIZE));
		}
	}
	for (cur = m2_anchor; cur != NULL; ) {
		m2_t *vic = cur;

		cur = cur->link;
		free(vic);
	}
	m2_anchor = NULL;
}

	m2_t *
m2_create(const char *id, size_t size)
{
	char buf[M2_ERROR_BUFSIZE];
	m2_t *result, *current;

	if (!m2_initialized) {
#if 0
		m2_abort("FATAL ERROR in m2_create - memory manager is not initialized!");
#else
		m2_init(m2_default_error_fun);
#endif	
	}
	if (size == 0) {
		sprintf(buf, "FATAL ERROR in m2_create - requested size for identifier %s is zero bytes!", id);
		m2_abort(buf);
	}

	for (current = m2_anchor;
			current != NULL;
			current = current->link)
	{
		if (!strncmp(id, current->id, M2_IDSIZE)) {
			sprintf(buf, "FATAL ERROR in m2_create - identifier %s is already in use!", id);
			m2_abort(buf);
		}
	}

	result = (m2_t *)malloc(sizeof(m2_t));

	if (result == NULL) {
		sprintf(buf, "FATAL ERROR in m2_create - failed to create \"%s\" handle!", id);
		m2_abort(buf);
	}

	strncpy(result->id, id, M2_IDSIZE);
	result->id[M2_IDSIZE - 1] = '\0';
	result->size = size;
	result->reused = 0;
	result->recycled = 0;
	result->newusage = 0;
	result->oldusage = 0;
	result->maxusage = 0;
	result->link = m2_anchor;
	m2_anchor = result;

	return result;
}

	void
m2_destroy(m2_t *handle)
{
	char buf[M2_ERROR_BUFSIZE];
	m2_t **curr;

	curr = &m2_anchor;
	for (;;) {
		if ((*curr) == NULL) {
			sprintf(buf,
					"FATAL ERROR in m2_destroy - handle %s "
					"missing from anchor chain!", handle->id);
			m2_abort(buf);
		}
		if ((*curr) == handle) {
			(*curr) = (*curr)->link;
			break;
		}
		curr = &(*curr)->link;
	}
	free(handle);
}

#ifdef M2_DEBUG
	char *
m2_report_debug(char *file, int line, char *buf, size_t size)
#else
	char *
m2_report(char *buf, size_t size)
#endif
{
	char local[512];
	m2_t *current;
	int64_t total_delta = 0;
	size_t offset = 0, delta;

#define M2_REPORT_COMMIT() {				\
	if (offset + delta >= size) goto bail;	\
	sprintf(buf + offset, "%s", local);		\
	offset += delta;						\
}

	delta = sprintf(local,
			"----------------------------------------"
			"----------------------------------------"
			"-----------------------------"
			"-----------------------------------\n");
	M2_REPORT_COMMIT();

	delta = sprintf(local, "%-30s  %9s %16s %16s %16s %16s %16s %16s\n",
			"id", "size", "current", "reused", "recycled", "maxusage",
			"absolute delta", "relative delta");
	M2_REPORT_COMMIT();

	delta = sprintf(local,
			"----------------------------------------"
			"----------------------------------------"
			"-----------------------------"
			"-----------------------------------\n");
	M2_REPORT_COMMIT();

	for (current = m2_anchor;
			current != NULL;
			current = current->link)
	{
		current->oldusage = current->newusage;
		current->newusage = current->reused - current->recycled;
		total_delta += (int64_t)current->newusage - (int64_t)current->oldusage;
	}

	for (current = m2_anchor;
			current != NULL;
			current = current->link)
	{
		int64_t delta = (int64_t)current->newusage - (int64_t)current->oldusage;

#define PRIu64 "lu"
#define PRId64 "ld"

		delta = sprintf(local,
				"%-30s  %9zu %16" PRIu64 " %16" PRIu64 " %16" PRIu64
				" %16" PRIu64 " %16" PRId64 " %16.2f%%\n",
				current->id, current->size,
				current->newusage, current->reused, current->recycled,
				current->maxusage, delta,
				(current->oldusage == 0) ? 0 : 100 * (float)delta / (float)current->oldusage);
		M2_REPORT_COMMIT();
	}
	
	delta += sprintf(local,
			"----------------------------------------"
			"----------------------------------------"
			"-----------------------------"
			"-----------------------------------\n");
	M2_REPORT_COMMIT();

	delta = sprintf(local, "%-30s  %9s %16" PRIu64 " %16" PRIu64 " %16" PRIu64 " %16" PRIu64 " %16" PRId64 "\n",
			m2_total.id, "",
			m2_total.reused - m2_total.recycled,
			m2_total.reused, m2_total.recycled,
			m2_total.maxusage, total_delta);
	M2_REPORT_COMMIT();

	delta = sprintf(local,
			"----------------------------------------"
			"----------------------------------------"
			"-----------------------------"
			"-----------------------------------\n");
	M2_REPORT_COMMIT();

	return buf;

bail:
#ifdef M2_DEBUG
	sprintf(local,
			"ERROR in m2_report, called from file \"%s\" line %d - "
			"target report buffer too small.", file, line);
	m2_error(local);
#else
	m2_error("ERROR in m2_report - target report buffer too small.");
#endif
	buf[offset] = '\0';
	return buf;
}

#ifdef M2_DEBUG
	void *
m2_reuse_debug(char *file, int line, m2_t *m, size_t n, bool z)
#else
	void *
m2_reuse(m2_t *m, size_t n, bool z)
#endif
{
#ifdef M2_DEBUG
	char buf[M2_ERROR_BUFSIZE];
#endif
	void *result;
	uint64_t usage;
	size_t bytes;

	if (m == NULL) {
#ifdef M2_DEBUG
		sprintf(buf,
				"FATAL ERROR in m2_reuse, called from file \"%s\" line %d - "
				"attempt to use an un-initialized (NULL) handle!", file, line);
		m2_abort(buf);
#else
		m2_abort("FATAL ERROR in m2_reuse - attempt to use an un-initialized (NULL) handle!");
#endif
	}

	if (n <= 0) {
#ifdef M2_DEBUG
		sprintf(buf,
				"FATAL ERROR in m2_reuse, called from file \"%s\" line %d - "
				"illegal to allocate zero (or less) bytes!", file, line);
		m2_abort(buf);
#else
		m2_abort("FATAL ERROR in m2_reuse - illegal to allocate zero (or less) bytes!");
#endif
	}

	bytes = n * m->size;

#ifdef M2_RECYCLE
	if (
			sizeof(void *) <= bytes &&
			bytes <= M2_RECYCLED_BLOCKSIZE &&
			m2_recycled_block[bytes - 1] != NULL)
	{
		/* Re-use recycled memory. */
		result = m2_recycled_block[bytes - 1];
		m2_recycled_block[bytes - 1] = M2_LINK(result);
		goto reused_recycled;
	}
#endif
	if ((bytes % M2_ALIGNMENT) > 0) {
		/* Non-aligned allocation. */
		result = malloc(bytes);
	} else {
		/* Aligned allocation. */
		int error = posix_memalign(&result, M2_ALIGNMENT, bytes);
		
		if (error) result = NULL;
	}
#ifdef M2_RECYCLE
reused_recycled:
#endif

	if (result == NULL) {
#ifdef M2_DEBUG
		sprintf(buf,
				"FATAL ERROR in m2_reuse, called from file \"%s\" line %d - "
				"failed to allocate memory!", file, line);
		m2_abort(buf);
#else
		m2_abort("FATAL ERROR in m2_reuse - failed to allocate memory!");
#endif
	}

	m->reused += bytes;

	usage = m->reused - m->recycled;

	if (usage > m->maxusage) {
		m->maxusage = usage;
	}

	m2_total.reused += bytes;

	usage = m2_total.reused - m2_total.recycled;

	if (usage > m2_total.maxusage) {
		m2_total.maxusage = usage;
	}

	if (z) memset(result, 0, bytes);

	return result;
}

#ifdef M2_DEBUG
	void
m2_recycle_debug(char *file, int line, m2_t *m, void *p, size_t n)
#else
	void
m2_recycle(m2_t *m, void *p, size_t n)
#endif
{
#ifdef M2_DEBUG
	char buf[M2_ERROR_BUFSIZE];
#endif
	size_t bytes;

	if (p == NULL) {
#ifdef M2_DEBUG
		sprintf(buf,
				"FATAL ERROR in m2_recycle, called from file \"%s\" line %d - "
				"illegal to recycle NULL pointer!", file, line);
		m2_abort(buf);
#else
		m2_abort("FATAL ERROR in m2_recycle - illegal to recycle NULL pointer!\n");
#endif
	}

	bytes = n * m->size;

	memset(p, 0, bytes);
	free(p);
	m->recycled += bytes;
	m2_total.recycled += bytes;
}

