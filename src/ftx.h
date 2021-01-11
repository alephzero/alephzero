#ifndef A0_SRC_FTX_H
#define A0_SRC_FTX_H

#include <a0/common.h>
#include <a0/errno.h>
#include <a0/time.h>

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "clock.h"
#include "macros.h"

// FUTEX_WAIT and FUTEX_WAIT_REQUEUE_PI default to CLOCK_MONOTONIC,
// but FUTEX_LOCK_PI always uses CLOCK_REALTIME.
//
// Until someone tells me otherwise, I assume this is bad decision making
// and I will instead standardize all things on CLOCK_BOOTTIME.

// Futex.
// Operations rely on the address.
// It should not be copied or moved.
typedef uint32_t a0_ftx_t;

A0_STATIC_INLINE
errno_t a0_futex(a0_ftx_t* uaddr,
                 int futex_op,
                 int val,
                 uintptr_t timeout_or_val2,
                 a0_ftx_t* uaddr2,
                 int val3) {
  A0_RETURN_ERR_ON_MINUS_ONE(syscall(SYS_futex, uaddr, futex_op, val, timeout_or_val2, uaddr2, val3));
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_ftx_wait(a0_ftx_t* ftx, int confirm_val, const timespec_t* timeout) {
  if (!timeout) {
    return a0_futex(ftx, FUTEX_WAIT, confirm_val, 0, NULL, 0);
  }

  timespec_t timeout_monotonic = a0_clock_convert(*timeout, CLOCK_BOOTTIME, CLOCK_MONOTONIC);
  return a0_futex(ftx, FUTEX_WAIT, confirm_val, (uintptr_t)&timeout_monotonic, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_wake(a0_ftx_t* ftx, int cnt) {
  return a0_futex(ftx, FUTEX_WAKE, cnt, 0, NULL, 0);
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
errno_t a0_ftx_lock_pi(a0_ftx_t* ftx, const timespec_t* timeout) {
  if (!timeout) {
    return a0_futex(ftx, FUTEX_LOCK_PI, 0, 0, NULL, 0);
  }

  timespec_t timeout_realtime = a0_clock_convert(*timeout, CLOCK_BOOTTIME, CLOCK_REALTIME);
  return a0_futex(ftx, FUTEX_LOCK_PI, 0, (uintptr_t)&timeout_realtime, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_trylock_pi(a0_ftx_t* ftx) {
  return a0_futex(ftx, FUTEX_TRYLOCK_PI, 0, 0, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_unlock_pi(a0_ftx_t* ftx) {
  return a0_futex(ftx, FUTEX_UNLOCK_PI, 0, 0, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_ftx_cmp_requeue_pi(a0_ftx_t* ftx, int confirm_val, int wake_cnt, a0_ftx_t* requeue_ftx, int max_requeue) {
  return a0_futex(ftx, FUTEX_CMP_REQUEUE_PI, wake_cnt, max_requeue, requeue_ftx, confirm_val);
}

A0_STATIC_INLINE
errno_t a0_ftx_wait_requeue_pi(a0_ftx_t* ftx, int confirm_val, const timespec_t* timeout, a0_ftx_t* requeue_ftx) {
  if (!timeout) {
    return a0_futex(ftx, FUTEX_WAIT_REQUEUE_PI, confirm_val, 0, requeue_ftx, 0);
  }

  timespec_t timeout_monotonic = a0_clock_convert(*timeout, CLOCK_BOOTTIME, CLOCK_MONOTONIC);
  return a0_futex(ftx, FUTEX_WAIT_REQUEUE_PI, confirm_val, (uintptr_t)&timeout_monotonic, requeue_ftx, 0);
}

#endif  // A0_SRC_FTX_H
