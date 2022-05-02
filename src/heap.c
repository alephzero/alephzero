#include <a0/heap.h>

#include "err_macro.h"

a0_err_t a0_heap_init(a0_heap_t* heap,
                      size_t obj_size,
                      a0_compare_t compare) {
  memset(heap, 0, sizeof(a0_heap_t));
  heap->_obj_size = obj_size;
  heap->_compare = compare;

  return A0_OK;
}

a0_err_t a0_heap_close(a0_heap_t* heap) {
  if (heap->_data) {
    free(heap->_data);
  }
  memset(heap, 0, sizeof(a0_heap_t));
  return A0_OK;
}

a0_err_t a0_heap_empty(a0_heap_t* heap, bool* is_empty) {
  *is_empty = (heap->_size == 0);
  return A0_OK;
}

a0_err_t a0_heap_size(a0_heap_t* heap, size_t* size) {
  *size = heap->_size;
  return A0_OK;
}

A0_STATIC_INLINE
void* _a0_heap_slot(a0_heap_t* heap, size_t idx) {
  return (uint8_t*)heap->_data + idx * heap->_obj_size;
}

A0_STATIC_INLINE
a0_err_t _a0_heap_grow(a0_heap_t* heap) {
  heap->_cap = heap->_cap ? (heap->_cap * 2) : 4;
  heap->_data = realloc(heap->_data, heap->_cap * heap->_obj_size);
  return A0_OK;
}

a0_err_t a0_heap_put(a0_heap_t* heap, const void* obj) {
  if (heap->_size + 1 >= heap->_cap) {
    A0_RETURN_ERR_ON_ERR(_a0_heap_grow(heap));
  }
  size_t idx = heap->_size++;
  void* slot = _a0_heap_slot(heap, idx);
  while (idx) {
    size_t parent_idx = (idx - 1) / 2;
    void* parent_slot = _a0_heap_slot(heap, parent_idx);
    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_compare_eval(heap->_compare, obj, parent_slot, &cmp));
    if (cmp >= 0) {
      break;
    }
    memcpy(slot, parent_slot, heap->_obj_size);
    idx = parent_idx;
    slot = parent_slot;
  }
  memcpy(slot, obj, heap->_obj_size);
  return A0_OK;
}

a0_err_t a0_heap_top(a0_heap_t* heap, const void** obj) {
  if (!heap->_size) {
    return EINVAL;
  }
  *obj = heap->_data;
  return A0_OK;
}

a0_err_t a0_heap_pop(a0_heap_t* heap, void* obj) {
  if (!heap->_size) {
    return EINVAL;
  }
  if (obj) {
    memcpy(obj, heap->_data, heap->_obj_size);
  }
  heap->_size--;
  if (!heap->_size) {
    return A0_OK;
  }

  obj = _a0_heap_slot(heap, heap->_size);
  size_t idx = 0;

  while (true) {
    size_t left_child_idx = 2 * idx + 1;
    size_t right_child_idx = 2 * idx + 2;

    if (left_child_idx >= heap->_size) {
      break;
    }

    size_t best_child_idx = left_child_idx;

    if (right_child_idx < heap->_size) {
      int cmp;
      A0_RETURN_ERR_ON_ERR(a0_compare_eval(
          heap->_compare,
          _a0_heap_slot(heap, left_child_idx),
          _a0_heap_slot(heap, right_child_idx),
          &cmp));
      if (cmp >= 0) {
        best_child_idx = right_child_idx;
      }
    }

    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_compare_eval(
        heap->_compare,
        obj,
        _a0_heap_slot(heap, best_child_idx),
        &cmp));

    if (cmp >= 0) {
      memcpy(_a0_heap_slot(heap, idx), _a0_heap_slot(heap, best_child_idx), heap->_obj_size);
      idx = best_child_idx;
    } else {
      break;
    }
  }
  memcpy(_a0_heap_slot(heap, idx), obj, heap->_obj_size);
  return A0_OK;
}
