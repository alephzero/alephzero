#ifndef A0_SRC_FTX_H
#define A0_SRC_FTX_H

#include <a0/common.h>
#include <a0/errno.h>

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "macros.h"

// Futex.
// Operations rely on the address.
// It should not be copied or moved.
typedef uint32_t a0_ftx_t;

A0_STATIC_INLINE
errno_t a0_futex(a0_ftx_t* addr1,
                 int op,
                 int val1,
                 const struct timespec* timeout,
                 a0_ftx_t* addr2,
                 int val3) {
  A0_RETURN_ERR_ON_MINUS_ONE(syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3));
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_ftx_wait(a0_ftx_t* ftx, int val, const struct timespec* to) {
  return a0_futex(ftx, FUTEX_WAIT, val, to, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_wake(a0_ftx_t* ftx, int nr) {
  return a0_futex(ftx, FUTEX_WAKE, nr, NULL, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_signal(a0_ftx_t* ftx) {
  return a0_ftx_wake(ftx, 1);
}

A0_STATIC_INLINE
errno_t a0_ftx_broadcast(a0_ftx_t* ftx) {
  return a0_ftx_wake(ftx, INT_MAX);
}

A0_STATIC_INLINE
errno_t a0_ftx_lock_pi(a0_ftx_t* ftx, int val1, const struct timespec* timeout) {
  return a0_futex(ftx, FUTEX_LOCK_PI, val1, timeout, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_unlock_pi(a0_ftx_t* ftx) {
  return a0_futex(ftx, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
}

#endif  // A0_SRC_FTX_H
