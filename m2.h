/**
 * @file m2.h
 * @author Mikael Sundstrom <micke@fabinv.com>
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
#ifndef M2
#define M2

#include <stdlib.h>
#include <stdbool.h>

#define M2_DEBUG

#define M2_ALIGNMENT 64

/**
 * @brief Maximum size of a memory handle identifier.
 */
#define M2_IDSIZE 256

/** @brief Memory management handle type definition (hidden). */
struct m2;
typedef struct m2 m2_t;

/**
 * @brief Initialize memory manager.
 *
 * @param error Function to be called if an error occurs.
 */
void m2_init(void (*error)(char *));

/**
 * @brief Exit memory manager.
 */
void m2_exit(void);

/**
 * @brief Create new memory management handle.
 *
 * @param id Handle identifier.
 * @param size Size of object associated with handle.
 *
 * @return Created and initialized handle.
 */
m2_t *m2_create(const char *id, size_t size);

/**
 * @brief Destroy memory management handle.
 *
 * @param handle Handle to destroy.
 */
void m2_destroy(m2_t *handle);

/**
 * @brief Print memory management report to output stream.
 *
 * @param stream Output stream.
 */
#	ifdef M2_DEBUG
#	define m2_report(buf, size) m2_report_debug(__FILE__, __LINE__, buf, size)
char *m2_report_debug(char *file, int line, char *buf, size_t size);
#else
char *m2_report(char *buf, size_t size);
#endif

/**
 * @brief Allocate memory.
 *
 * Allocate memory for an array of objects of the size associated with handle.
 *
 * @param m Memory management handle.
 * @param n Number of objects to allocate memory for.
 * @param z Boolean true sets allocated memory to zero.
 *
 * @return Address of allocated memory block.
 */
#	ifdef M2_DEBUG
#	define m2_reuse(m, n, z) m2_reuse_debug(__FILE__, __LINE__, m, n, z)
void *m2_reuse_debug(char *file, int line, m2_t *m, size_t n, bool z);
#	else
void *m2_reuse(m2_t *m, size_t n, bool z);
#	endif

/**
 * @brief Deallocate memory.
 *
 * Deallocate memory for an array of objects previously allocated.
 *
 * @param m Memory management handle.
 * @param p Memory address to start of block t deallocate.
 * @param n Number of objects that was previously alocated.
 */
#	ifdef M2_DEBUG
#	define m2_recycle(m, p, n) m2_recycle_debug(__FILE__, __LINE__, m, p, n)
void m2_recycle_debug(char *file, int line, m2_t *m, void *p, size_t n);
#	else
void m2_recycle(m2_t *m, void *p, size_t n);
#	endif

#endif /* M2 */
