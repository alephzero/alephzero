#ifndef A0_INTERNAL_MACROS_H
#define A0_INTERNAL_MACROS_H

#include <errno.h>

#define A0_CAT(a, b) A0_CAT_(a, b)
#define A0_CAT_(a, b) a##b

#define A0_LIKELY(x) __builtin_expect((x), 1)
#define A0_UNLIKELY(x) __builtin_expect((x), 0)

#define A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {        \
    return errno;                      \
  }

#define A0_INTERNAL_CLEANUP_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {        \
    goto cleanup;                      \
  }

#define A0_INTERNAL_RETURN_ERR_ON_ERR(x)                          \
  errno_t A0_CAT(_a0_var_, __LINE__) = (x);               \
  if (A0_UNLIKELY(A0_CAT(_a0_var_, __LINE__) != A0_OK)) { \
    return A0_CAT(_a0_var_, __LINE__);                    \
  }

#endif  // A0_INTERNAL_MACROS_H
