#include <a0/align.h>
#include <a0/vec.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

a0_err_t a0_vec_init(a0_vec_t* vec, size_t obj_size) {
  *vec = (a0_vec_t)A0_EMPTY;
  vec->_obj_size = obj_size;
  vec->_slot_size = a0_align(obj_size);
  return A0_OK;
}

a0_err_t a0_vec_close(a0_vec_t* vec) {
  if (vec->_data) {
    free(vec->_data);
  }
  *vec = (a0_vec_t)A0_EMPTY;
  return A0_OK;
}

a0_err_t a0_vec_obj_size(a0_vec_t* vec, size_t* obj_size) {
  *obj_size = vec->_obj_size;
  return A0_OK;
}

a0_err_t a0_vec_empty(a0_vec_t* vec, bool* is_empty) {
  *is_empty = (vec->_size == 0);
  return A0_OK;
}

a0_err_t a0_vec_size(a0_vec_t* vec, size_t* size) {
  *size = vec->_size;
  return A0_OK;
}

a0_err_t a0_vec_at(a0_vec_t* vec, size_t idx, void** out) {
  if (idx >= vec->_size) {
    return A0_ERR_RANGE;
  }
  *out = (uint8_t*)vec->_data + idx * vec->_slot_size;
  return A0_OK;
}

a0_err_t a0_vec_front(a0_vec_t* vec, void** out) {
  return a0_vec_at(vec, 0, out);
}

a0_err_t a0_vec_back(a0_vec_t* vec, void** out) {
  if (vec->_size == 0) {
    return A0_ERR_RANGE;
  }
  return a0_vec_at(vec, vec->_size - 1, out);
}

a0_err_t a0_vec_resize(a0_vec_t* vec, size_t size) {
  bool need_realloc = false;
  while (size > vec->_cap) {
    vec->_cap = vec->_cap ? (size_t)(vec->_cap * 1.5) : 4;
    need_realloc = true;
  }
  if (need_realloc) {
    vec->_data = realloc(vec->_data, vec->_cap * vec->_slot_size);
  }
  vec->_size = size;
  return A0_OK;
}

a0_err_t a0_vec_push_back(a0_vec_t* vec, const void* obj) {
  A0_RETURN_ERR_ON_ERR(a0_vec_resize(vec, vec->_size + 1));

  void* dst;
  a0_vec_back(vec, &dst);
  memcpy(dst, obj, vec->_obj_size);

  return A0_OK;
}

a0_err_t a0_vec_pop_back(a0_vec_t* vec, void* out) {
  if (vec->_size == 0) {
    return A0_ERR_RANGE;
  }

  return a0_vec_swap_back_pop(vec, vec->_size - 1, out);
}

a0_err_t a0_vec_swap_back_pop(a0_vec_t* vec, size_t idx, void* out) {
  if (idx >= vec->_size) {
    return A0_ERR_RANGE;
  }

  void* slot;
  a0_vec_at(vec, idx, &slot);

  if (out) {
    memcpy(out, slot, vec->_obj_size);
  }

  if (idx + 1 != vec->_size) {
    void* old_back;
    a0_vec_back(vec, &old_back);
    memcpy(slot, old_back, vec->_obj_size);
  }

  vec->_size--;
  return A0_OK;
}
