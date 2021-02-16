#ifndef A0_SRC_ERR_UTIL_H
#define A0_SRC_ERR_UTIL_H

#include <errno.h>

#define A0_RETURN_ERR_ON_MINUS_ONE(X) \
  do {                                \
    if ((X) == -1) {                  \
      return errno;                   \
    }                                 \
  } while (0)

#define A0_RETURN_ERR_ON_ERR(X) \
  do {                          \
    errno_t _err = (X);         \
    if (_err != A0_OK) {        \
      return _err;              \
    }                           \
  } while (0)

#endif  // A0_SRC_ERR_UTIL_H
