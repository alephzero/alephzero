#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/errno.h>

#include <errno.h>
#include <stdlib.h>

#include "macros.h"

A0_STATIC_INLINE
errno_t malloc_alloc_impl(void* user_data, size_t size, a0_buf_t* out) {
  (void)user_data;
  out->size = size;
  out->ptr = malloc(size);
  if (!out->ptr) {
    return errno;
  }
  return A0_OK;
}

A0_STATIC_INLINE
errno_t malloc_dealloc_impl(void* user_data, a0_buf_t buf) {
  (void)user_data;
  if (buf.ptr) {
    free(buf.ptr);
  }
  return A0_OK;
}

errno_t a0_malloc_allocator_init(a0_alloc_t* alloc) {
  *alloc = (a0_alloc_t){
      .user_data = NULL,
      .alloc = &malloc_alloc_impl,
      .dealloc = &malloc_dealloc_impl,
  };
  return A0_OK;
}

errno_t a0_malloc_allocator_close(a0_alloc_t* alloc) {
  (void)alloc;
  return A0_OK;
}

A0_STATIC_INLINE
errno_t realloc_alloc_impl(void* user_data, size_t size, a0_buf_t* out) {
  a0_buf_t* data = (a0_buf_t*)user_data;
  if (data->size < size) {
    data->ptr = realloc(data->ptr, size);
    data->size = size;
  }
  out->ptr = data->ptr;
  // Note: `data->size` is the capacity. `size` is the current size.
  out->size = size;
  if (!out->ptr) {
    return errno;
  }
  return A0_OK;
}

errno_t a0_realloc_allocator_init(a0_alloc_t* alloc) {
  a0_buf_t* data = malloc(sizeof(a0_buf_t));
  if (!data) {
    return errno;
  }
  *data = (a0_buf_t){
      .ptr = malloc(1),
      .size = 1,
  };
  if (!data->ptr) {
    free(data);
    return errno;
  }
  *alloc = (a0_alloc_t){
      .user_data = data,
      .alloc = &realloc_alloc_impl,
      .dealloc = NULL,
  };
  return A0_OK;
}

errno_t a0_realloc_allocator_close(a0_alloc_t* alloc) {
  if (!alloc->user_data) {
    return EBADF;
  }
  a0_buf_t* data = (a0_buf_t*)alloc->user_data;
  free(data->ptr);
  free(data);
  alloc->user_data = NULL;
  return A0_OK;
}
