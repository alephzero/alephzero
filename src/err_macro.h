#ifndef A0_SRC_ERR_MACRO_H
#define A0_SRC_ERR_MACRO_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

A0_STATIC_INLINE
a0_err_t A0_MAKE_SYSERR(int syserr) {
  if (syserr) {
    a0_err_syscode = syserr;
    return A0_ERRCODE_SYSERR;
  }
  return A0_OK;
}

A0_STATIC_INLINE
int A0_SYSERR(a0_err_t err) {
  return err == A0_ERRCODE_SYSERR ? a0_err_syscode : 0;
}

A0_STATIC_INLINE_RECURSIVE
a0_err_t A0_MAKE_MSGERR(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (fmt) {
    vsnprintf(a0_err_msg, sizeof(a0_err_msg), fmt, args);
    va_end(args);
    return A0_ERRCODE_CUSTOM_MSG;
  }
  va_end(args);
  return A0_OK;
}

#define A0_RETURN_SYSERR_ON_MINUS_ONE(X) \
  do {                                   \
    if ((X) == -1) {                     \
      a0_err_syscode = errno;            \
      return A0_ERRCODE_SYSERR;          \
    }                                    \
  } while (0)

#define A0_RETURN_ERR_ON_ERR(X) \
  do {                          \
    a0_err_t _err = (X);        \
    if (_err) {                 \
      return _err;              \
    }                           \
  } while (0)

#endif  // A0_SRC_ERR_MACRO_H
