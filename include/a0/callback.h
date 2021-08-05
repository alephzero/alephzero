#ifndef A0_CALLBACK_H
#define A0_CALLBACK_H

#include <a0/err.h>
#include <a0/inline.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_callback_s {
  void* user_data;
  void (*fn)(void* user_data);
} a0_callback_t;

A0_STATIC_INLINE
void a0_callback_call(a0_callback_t callback) {
  if (callback.fn) {
    callback.fn(callback.user_data);
  }
}

typedef struct a0_predicate_s {
  void* user_data;
  errno_t (*fn)(void* user_data, bool*);
} a0_predicate_t;

A0_STATIC_INLINE
errno_t a0_predicate_eval(a0_predicate_t pred, bool* out) {
  return pred.fn(pred.user_data, out);
}

/// Comparison function signature.
///
/// The output of this function is used as the result of a comparison
/// operation.
/// Negative values are used to indicate that the first operand is less
/// than the second operand.
/// Positive values are used to indicate that the first operand is greater
/// than the second operand.
/// Zero values are used to indicate that the two operands are equal.
typedef struct a0_compare_s {
  void* user_data;
  errno_t (*fn)(void* user_data, const void* lhs, const void* rhs, int* out);
} a0_compare_t;

/// Invoke a comparison function.
A0_STATIC_INLINE
errno_t a0_compare_eval(a0_compare_t cmp, const void* lhs, const void* rhs, int* out) {
  return cmp.fn(cmp.user_data, lhs, rhs, out);
}

typedef struct a0_hash_s {
  void* user_data;
  errno_t (*fn)(void* user_data, const void* data, size_t* out);
} a0_hash_t;

A0_STATIC_INLINE
errno_t a0_hash_eval(a0_hash_t hash, const void* data, size_t* out) {
  return hash.fn(hash.user_data, data, out);
}

#ifdef __cplusplus
}
#endif

#endif  // A0_CALLBACK_H
