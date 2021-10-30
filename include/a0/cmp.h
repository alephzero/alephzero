#ifndef A0_CMP_H
#define A0_CMP_H

#include <a0/err.h>
#include <a0/inline.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Comparison function signature.
///
/// The output of this function is used as the result of a comparison
/// operation.
/// Negative values are used to indicate that the first operand is less
/// than the second operand.
/// Positive values are used to indicate that the first operand is greater
/// than the second operand.
/// Zero values are used to indicate that the two operands are equal.
typedef struct a0_cmp_s {
  void* user_data;
  a0_err_t (*fn)(void* user_data, const void* lhs, const void* rhs, int* out);
} a0_cmp_t;

/// Invoke a comparison function.
A0_STATIC_INLINE
a0_err_t a0_cmp_eval(a0_cmp_t cmp, const void* lhs, const void* rhs, int* out) {
  return cmp.fn(cmp.user_data, lhs, rhs, out);
}

typedef struct a0_hash_s {
  void* user_data;
  a0_err_t (*fn)(void* user_data, const void* data, size_t* out);
} a0_hash_t;

A0_STATIC_INLINE
a0_err_t a0_hash_eval(a0_hash_t hash, const void* data, size_t* out) {
  return hash.fn(hash.user_data, data, out);
}

extern const a0_hash_t A0_HASH_U32;
extern const a0_cmp_t A0_CMP_U32;

extern const a0_hash_t A0_HASH_PTR;
extern const a0_cmp_t A0_CMP_PTR;

extern const a0_hash_t A0_HASH_BUF;
extern const a0_cmp_t A0_CMP_BUF;

extern const a0_hash_t A0_HASH_STR;
extern const a0_cmp_t A0_CMP_STR;

extern const a0_hash_t A0_HASH_UUID;
extern const a0_cmp_t A0_CMP_UUID;

#ifdef __cplusplus
}
#endif

#endif  // A0_CMP_H
