#ifndef A0_ALLOC_H
#define A0_ALLOC_H

#include <a0/common.h>
#include <a0/errno.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Defines a strategy for allocating memory buffers.
typedef struct a0_alloc_s {
  /// User data to be passed as context to other a0_alloc_t methods.
  void* user_data;
  /// Allocates a memory buffer of a given size.
  errno_t (*alloc)(void* user_data, size_t size, a0_buf_t* out);
  /// Deallocates a memory buffer previously allocated with this alloc.
  errno_t (*dealloc)(void* user_data, a0_buf_t);
} a0_alloc_t;

/// Perform allocation.
static inline errno_t a0_alloc(a0_alloc_t alloc, size_t size, a0_buf_t* out) {
  return alloc.alloc(alloc.user_data, size, out);
}

/// Perform deallocation.
static inline errno_t a0_dealloc(a0_alloc_t alloc, a0_buf_t buf) {
  if (alloc.dealloc) {
    return alloc.dealloc(alloc.user_data, buf);
  }
  return A0_OK;
}

/** \addtogroup MALLOC_ALLOCATOR
 *  @{
 * Each call to alloc allocates a new, independent buffer.
 *
 * Allocs must be explicitly freed by the user.
 */
errno_t a0_malloc_allocator_init(a0_alloc_t*);
errno_t a0_malloc_allocator_close(a0_alloc_t*);
/** @}*/

/** \addtogroup REALLOC_ALLOCATOR
 *  @{
 * Each call to alloc reuses the same buffer space.
 *
 * Allocs may NOT be explicitly freed by the user.
 */
errno_t a0_realloc_allocator_init(a0_alloc_t*);
errno_t a0_realloc_allocator_close(a0_alloc_t*);
/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_ALLOC_H
