#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/tid.h>
#include <a0/time.h>

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "atomic.h"
#include "clock.h"
#include "err_macro.h"
#include "ftx.h"
#include "robust.h"
#include "tsan.h"

A0_STATIC_INLINE
a0_err_t a0_mtx_timedlock_robust(a0_mtx_t* mtx, a0_time_mono_t* timeout) {
  const uint32_t tid = a0_tid();

  int syserr = EINTR;
  while (syserr == EINTR) {
    // Try to lock without kernel involvement.
    if (a0_cas(&mtx->ftx, 0, tid)) {
      return A0_OK;
    }

    // Ask the kernel to lock.
    syserr = A0_SYSERR(a0_ftx_lock_pi(&mtx->ftx, timeout));
  }

  if (!syserr) {
    if (a0_ftx_owner_died(a0_atomic_load(&mtx->ftx))) {
      return A0_MAKE_SYSERR(EOWNERDEAD);
    }
    return A0_OK;
  }

  return A0_MAKE_SYSERR(syserr);
}

a0_err_t a0_mtx_timedlock(a0_mtx_t* mtx, a0_time_mono_t* timeout) {
  // Note: __tsan_mutex_pre_lock should come here, but tsan doesn't provide
  //       a way to "fail" a lock. Only a trylock.
  a0_robust_op_start(mtx);
  const a0_err_t err = a0_mtx_timedlock_robust(mtx, timeout);
  if (a0_mtx_lock_successful(err)) {
    __tsan_mutex_pre_lock(mtx, 0);
    a0_robust_op_add(mtx);
    __tsan_mutex_post_lock(mtx, 0, 0);
  }
  a0_robust_op_end(mtx);
  return err;
}

a0_err_t a0_mtx_lock(a0_mtx_t* mtx) {
  return a0_mtx_timedlock(mtx, NULL);
}

A0_STATIC_INLINE
a0_err_t a0_mtx_trylock_impl(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  // Try to lock without kernel involvement.
  uint32_t old = a0_cas_val(&mtx->ftx, 0, tid);

  // Did it work?
  if (!old) {
    return A0_OK;
  }

  // Is the owner still alive?
  if (!a0_ftx_owner_died(old)) {
    return A0_MAKE_SYSERR(EBUSY);
  }

  // Oh, the owner died. Ask the kernel to fix the state.
  a0_err_t err = a0_ftx_trylock_pi(&mtx->ftx);
  if (!err) {
    if (a0_ftx_owner_died(a0_atomic_load(&mtx->ftx))) {
      return A0_MAKE_SYSERR(EOWNERDEAD);
    }
    return A0_OK;
  }

  // EAGAIN means that somebody else beat us to it.
  // Anything else means we're borked.
  if (A0_SYSERR(err) == EAGAIN) {
    return A0_MAKE_SYSERR(EBUSY);
  }
  return err;
}

a0_err_t a0_mtx_trylock(a0_mtx_t* mtx) {
  __tsan_mutex_pre_lock(mtx, __tsan_mutex_try_lock);
  a0_robust_op_start(mtx);
  a0_err_t err = a0_mtx_trylock_impl(mtx);
  if (a0_mtx_lock_successful(err)) {
    a0_robust_op_add(mtx);
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock, 0);
  } else {
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  }
  a0_robust_op_end(mtx);
  return err;
}

a0_err_t a0_mtx_unlock(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  const uint32_t val = a0_atomic_load(&mtx->ftx);

  // Only the owner can unlock.
  if (a0_ftx_tid(val) != tid) {
    return A0_MAKE_SYSERR(EPERM);
  }

  __tsan_mutex_pre_unlock(mtx, 0);

  a0_robust_op_start(mtx);
  a0_robust_op_del(mtx);

  a0_atomic_and_fetch(&mtx->ftx, ~FUTEX_OWNER_DIED);

  // If the futex is exactly equal to tid, then there are no waiters and the
  // kernel doesn't need to get involved.
  if (!a0_cas(&mtx->ftx, tid, 0)) {
    // Ask the kernel to wake up a waiter.
    a0_ftx_unlock_pi(&mtx->ftx);
  }

  a0_robust_op_end(mtx);
  __tsan_mutex_post_unlock(mtx, 0);

  return A0_OK;
}

bool a0_mtx_lock_successful(a0_err_t err) {
  return !err || a0_mtx_previous_owner_died(err);
}

bool a0_mtx_previous_owner_died(a0_err_t err) {
  return A0_SYSERR(err) == EOWNERDEAD;
}

a0_err_t a0_cnd_timedwait(a0_cnd_t* cnd, a0_mtx_t* mtx, a0_time_mono_t* timeout) {
  if (timeout) {
    // Let's not unlock the mutex if we're going to get EINVAL due to a bad timeout.
    if ((timeout->ts.tv_sec < 0 || timeout->ts.tv_nsec < 0 || (!timeout->ts.tv_sec && !timeout->ts.tv_nsec) || timeout->ts.tv_nsec >= NS_PER_SEC)) {
      return A0_MAKE_SYSERR(EINVAL);
    }
  }

  const uint32_t init_cnd = a0_atomic_load(cnd);

  // Unblock other threads to do the things that will eventually signal this wait.
  a0_err_t err = a0_mtx_unlock(mtx);
  if (err) {
    return err;
  }

  __tsan_mutex_pre_lock(mtx, 0);
  a0_robust_op_start(mtx);

  do {
    // Priority-inheritance-aware wait until awoken or timeout.
    err = a0_ftx_wait_requeue_pi(cnd, init_cnd, timeout, &mtx->ftx);
  } while (A0_SYSERR(err) == EINTR);

  // We need to manually lock on timeout.
  // Note: We keep the timeout error.
  if (A0_SYSERR(err) == ETIMEDOUT) {
    a0_mtx_timedlock_robust(mtx, NULL);
  }
  // Someone else grabbed and mutated the resource between the unlock and wait.
  // No need to wait.
  if (A0_SYSERR(err) == EAGAIN) {
    err = a0_mtx_timedlock_robust(mtx, NULL);
  }

  a0_robust_op_add(mtx);

  // If no higher priority error, check the previous owner didn't die.
  if (!err) {
    err = a0_ftx_owner_died(a0_atomic_load(&mtx->ftx)) ? EOWNERDEAD : A0_OK;
  }

  a0_robust_op_end(mtx);
  __tsan_mutex_post_lock(mtx, 0, 0);
  return err;
}

a0_err_t a0_cnd_wait(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_timedwait(cnd, mtx, NULL);
}

A0_STATIC_INLINE
a0_err_t a0_cnd_wake(a0_cnd_t* cnd, a0_mtx_t* mtx, uint32_t cnt) {
  uint32_t val = a0_atomic_add_fetch(cnd, 1);

  while (true) {
    a0_err_t err = a0_ftx_cmp_requeue_pi(cnd, val, &mtx->ftx, cnt);
    if (A0_SYSERR(err) != EAGAIN) {
      return err;
    }

    // Another thread is also trying to wake this condition variable.
    val = a0_atomic_load(cnd);
  }
}

a0_err_t a0_cnd_signal(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, 1);
}

a0_err_t a0_cnd_broadcast(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, INT_MAX);
}
