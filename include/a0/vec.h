#ifndef A0_VEC_H
#define A0_VEC_H

#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_vec_s {
  size_t _slot_size;
  size_t _obj_size;

  size_t _size;
  size_t _cap;

  uint8_t* _data;
} a0_vec_t;

a0_err_t a0_vec_init(a0_vec_t*, size_t obj_size);

a0_err_t a0_vec_close(a0_vec_t*);

a0_err_t a0_vec_obj_size(a0_vec_t*, size_t* obj_size);

a0_err_t a0_vec_empty(a0_vec_t*, bool* is_empty);

a0_err_t a0_vec_size(a0_vec_t*, size_t* size);

a0_err_t a0_vec_resize(a0_vec_t*, size_t);

a0_err_t a0_vec_at(a0_vec_t*, size_t idx, void** out);

a0_err_t a0_vec_front(a0_vec_t*, void** out);

a0_err_t a0_vec_back(a0_vec_t*, void** out);

a0_err_t a0_vec_push_back(a0_vec_t*, const void*);

a0_err_t a0_vec_pop_back(a0_vec_t*, void* out);

a0_err_t a0_vec_swap_back_pop(a0_vec_t*, size_t idx, void* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_VEC_H
