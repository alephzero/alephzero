#include <a0/cmp.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/heap.h>
#include <a0/vec.h>

#include <stdbool.h>
#include <string.h>

#include "err_macro.h"

a0_err_t a0_heap_init(a0_heap_t* heap,
                      size_t obj_size,
                      a0_cmp_t cmp) {
  *heap = (a0_heap_t)A0_EMPTY;
  heap->_cmp = cmp;
  return a0_vec_init(&heap->_vec, obj_size);
}

a0_err_t a0_heap_close(a0_heap_t* heap) {
  return a0_vec_close(&heap->_vec);
}

a0_err_t a0_heap_empty(a0_heap_t* heap, bool* is_empty) {
  return a0_vec_empty(&heap->_vec, is_empty);
}

a0_err_t a0_heap_size(a0_heap_t* heap, size_t* size) {
  return a0_vec_size(&heap->_vec, size);
}

a0_err_t a0_heap_put(a0_heap_t* heap, const void* obj) {
  size_t obj_size;
  a0_vec_obj_size(&heap->_vec, &obj_size);

  size_t idx;
  a0_vec_size(&heap->_vec, &idx);
  a0_vec_resize(&heap->_vec, idx + 1);

  void* slot;
  a0_vec_back(&heap->_vec, &slot);

  while (idx) {
    size_t parent_idx = (idx - 1) / 2;
    void* parent_slot;
    a0_vec_at(&heap->_vec, parent_idx, &parent_slot);
    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_cmp_eval(heap->_cmp, obj, parent_slot, &cmp));
    if (cmp >= 0) {
      break;
    }
    memcpy(slot, parent_slot, obj_size);
    idx = parent_idx;
    slot = parent_slot;
  }
  memcpy(slot, obj, obj_size);
  return A0_OK;
}

a0_err_t a0_heap_top(a0_heap_t* heap, const void** obj) {
  return a0_vec_front(&heap->_vec, (void**)obj);
}

a0_err_t a0_heap_pop(a0_heap_t* heap, void* out) {
  size_t obj_size;
  a0_vec_obj_size(&heap->_vec, &obj_size);

  size_t old_size;
  a0_vec_size(&heap->_vec, &old_size);

  if (!old_size) {
    return A0_ERR_AGAIN;
  }

  if (out) {
    void* slot;
    a0_vec_front(&heap->_vec, &slot);
    memcpy(out, slot, obj_size);
  }

  size_t new_size = old_size - 1;
  if (!new_size) {
    a0_vec_pop_back(&heap->_vec, NULL);
    return A0_OK;
  }

  void* obj;
  a0_vec_back(&heap->_vec, &obj);
  size_t idx = 0;

  while (true) {
    size_t left_child_idx = 2 * idx + 1;
    size_t right_child_idx = 2 * idx + 2;

    if (left_child_idx >= new_size) {
      break;
    }

    void* left_child_slot;
    a0_vec_at(&heap->_vec, left_child_idx, &left_child_slot);
    void* right_child_slot;
    a0_vec_at(&heap->_vec, right_child_idx, &right_child_slot);

    size_t best_child_idx = left_child_idx;
    void* best_child_slot = left_child_slot;

    if (right_child_idx < new_size) {
      int cmp;
      A0_RETURN_ERR_ON_ERR(a0_cmp_eval(heap->_cmp, left_child_slot, right_child_slot, &cmp));
      if (cmp >= 0) {
        best_child_idx = right_child_idx;
        best_child_slot = right_child_slot;
      }
    }

    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_cmp_eval(heap->_cmp, obj, best_child_slot, &cmp));

    if (cmp >= 0) {
      void* parent_slot;
      a0_vec_at(&heap->_vec, idx, &parent_slot);
      memcpy(parent_slot, best_child_slot, obj_size);
      idx = best_child_idx;
    } else {
      break;
    }
  }
  void* parent_slot;
  a0_vec_at(&heap->_vec, idx, &parent_slot);
  memcpy(parent_slot, obj, obj_size);
  a0_vec_pop_back(&heap->_vec, NULL);
  return A0_OK;
}
