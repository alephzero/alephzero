#ifndef A0_HEAP_H
#define A0_HEAP_H

#include <a0/cmp.h>
#include <a0/err.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_heap_s {
  size_t _obj_size;
  a0_cmp_t _cmp;

  size_t _size;
  size_t _cap;

  uint8_t* _data;
} a0_heap_t;

a0_err_t a0_heap_init(a0_heap_t*, size_t obj_size, a0_cmp_t);

a0_err_t a0_heap_close(a0_heap_t*);

a0_err_t a0_heap_empty(a0_heap_t*, bool* is_empty);

a0_err_t a0_heap_size(a0_heap_t*, size_t* size);

a0_err_t a0_heap_put(a0_heap_t*, const void*);

a0_err_t a0_heap_top(a0_heap_t*, const void**);

a0_err_t a0_heap_pop(a0_heap_t*, void*);

// put+pop maybe implemented more efficiently than calling the two...

#ifdef __cplusplus
}
#endif

#endif  // A0_HEAP_H
