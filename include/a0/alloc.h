#ifndef A0_ALLOC_H
#define A0_ALLOC_H

#include <a0/common.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Defines a strategy for allocating memory buffers.
///
/// TODO: Add explicit deallocation. For now, that falls on the user.
typedef struct a0_alloc_s {
  /// User data to be passed as context to other a0_alloc_t methods.
  void* user_data;
  /// Allocates a memory buffer of a given size.
  errno_t (*fn)(void* user_data, size_t size, a0_buf_t* out);
} a0_alloc_t;

/// Perform allocation.
static inline
errno_t a0_alloc(a0_alloc_t alloc, size_t size, a0_buf_t* out) {
  return alloc.fn(alloc.user_data, size, out);
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
