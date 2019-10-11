#ifndef A0_ALLOC_H
#define A0_ALLOC_H

#include <a0/common.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_alloc_s {
  void* user_data;
  void (*fn)(void* user_data, size_t size, a0_buf_t*);
} a0_alloc_t;

a0_alloc_t a0_malloc_allocator();
void a0_free_malloc_allocator(a0_alloc_t);

a0_alloc_t a0_realloc_allocator();
void a0_free_realloc_allocator(a0_alloc_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_ALLOC_H
