#ifndef A0_ERR_H
#define A0_ERR_H

#include <a0/macros.h>
#include <errno.h>

typedef int errno_t;

#define A0_OK 0

#define _A0_RETURN_ERR_ON_MINUS_ONE(x)  \
  if (A0_UNLIKELY((x) == -1)) {         \
    return errno;                      \
  }

#define _A0_RETURN_ERR_ON_ERR(x)                           \
  errno_t A0_CAT(_a0_var_, __LINE__) = (x);                \
  if (A0_UNLIKELY(A0_CAT(_a0_var_, __LINE__) != A0_OK)) {  \
    return A0_CAT(_a0_var_, __LINE__);                     \
  }

#endif
