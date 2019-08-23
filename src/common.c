#include <a0/alloc.h>

#include <stdlib.h>

#include "macros.h"

A0_STATIC_INLINE
void a0_malloc_allocator_impl(void* unused, size_t size, a0_buf_t* out) {
  (void)unused;
  out->size = size;
  out->ptr = malloc(size);
}

a0_alloc_t a0_malloc_allocator() {
  return (a0_alloc_t){
      .user_data = NULL,
      .fn = &a0_malloc_allocator_impl,
  };
}

void a0_free_malloc_allocator(a0_alloc_t unused) {
  (void)unused;
}

A0_STATIC_INLINE
void a0_realloc_allocator_impl(void* user_data, size_t size, a0_buf_t* out) {
  a0_buf_t* data = (a0_buf_t*)user_data;
  if (data->size < size) {
    data->ptr = realloc(data->ptr, size);
    data->size = size;
  }
  out->ptr = data->ptr;
  // Note: `data->size` is the capacity. `size` is the current size.
  out->size = size;
}

a0_alloc_t a0_realloc_allocator() {
  a0_buf_t* data = malloc(sizeof(a0_buf_t));
  *data = (a0_buf_t){
      .ptr = malloc(1),
      .size = 1,
  };
  return (a0_alloc_t){
      .user_data = data,
      .fn = &a0_realloc_allocator_impl,
  };
}

void a0_free_realloc_allocator(a0_alloc_t alloc) {
  a0_buf_t* data = (a0_buf_t*)alloc.user_data;
  free(data->ptr);
  free(data);
}
