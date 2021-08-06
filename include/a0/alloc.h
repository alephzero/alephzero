#ifndef A0_ALLOC_H
#define A0_ALLOC_H

#include <a0/buf.h>
#include <a0/err.h>
#include <a0/inline.h>

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
A0_STATIC_INLINE
errno_t a0_alloc(a0_alloc_t alloc, size_t size, a0_buf_t* out) {
  return alloc.alloc(alloc.user_data, size, out);
}

/// Perform deallocation.
A0_STATIC_INLINE
errno_t a0_dealloc(a0_alloc_t alloc, a0_buf_t buf) {
  if (alloc.dealloc) {
    return alloc.dealloc(alloc.user_data, buf);
  }
  return A0_OK;
}

#ifdef __cplusplus
}
#endif

#endif  // A0_ALLOC_H
