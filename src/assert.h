#ifndef A0_SRC_ASSERT_H
#define A0_SRC_ASSERT_H

#ifdef DEBUG

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define A0_ASSERT(X, MSG, ...)                           \
  do {                                                   \
    if (!(X)) {                                          \
      fprintf(stderr, "AlephZero Assertion Failed!\n");  \
      fprintf(stderr, "File: %s\n", __FILE__);           \
      fprintf(stderr, "Line: %d\n", __LINE__);           \
      fprintf(stderr, "Func: %s\n", __func__);           \
      fprintf(stderr, "Expr: %s\n", #X);                 \
      fprintf(stderr, "Msg:  " MSG "\n", ##__VA_ARGS__); \
      abort();                                           \
    }                                                    \
  } while (0)

#define A0_ASSERT_OK(ERR, MSG, ...)                                \
  do {                                                             \
    a0_err_t _err = (ERR);                                         \
    if (_err) {                                                    \
      fprintf(stderr, "AlephZero Assertion Failed!\n");            \
      fprintf(stderr, "File: %s\n", __FILE__);                     \
      fprintf(stderr, "Line: %d\n", __LINE__);                     \
      fprintf(stderr, "Func: %s\n", __func__);                     \
      fprintf(stderr, "Expr: %s\n", #ERR);                         \
      fprintf(stderr, "Err:  [%d] %s\n", _err, a0_strerror(_err)); \
      fprintf(stderr, "Msg:  " MSG "\n", ##__VA_ARGS__);           \
      abort();                                                     \
    }                                                              \
  } while (0)

#else

#include "unused.h"

#define A0_ASSERT(X, ...) \
  do {                    \
    A0_MAYBE_UNUSED(X);   \
  } while (0)

#define A0_ASSERT_OK(ERR, ...) \
  do {                         \
    A0_MAYBE_UNUSED(ERR);      \
  } while (0)

#endif

#endif  // A0_SRC_ASSERT_H
