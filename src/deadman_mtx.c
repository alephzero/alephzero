#include <a0/deadman_mtx.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/tid.h>
#include <a0/time.h>

#include <errno.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <stdint.h>

#include "atomic.h"
#include "err_macro.h"
#include "ftx.h"
#include "robust.h"
#include "tsan.h"

a0_err_t a0_deadman_mtx_init(a0_deadman_mtx_t* d, a0_deadman_mtx_shared_token_t* shared_token) {
  *d = (a0_deadman_mtx_t)A0_EMPTY;
  d->_stkn = shared_token;
  return A0_OK;
}

a0_err_t a0_deadman_mtx_shutdown(a0_deadman_mtx_t* d) {
  A0_TSAN_HAPPENS_BEFORE(&d->_shutdown);
  a0_atomic_store(&d->_shutdown, true);
  while (a0_atomic_load(&d->_inop)) {
    // Yes, this is awful. Find a better way to do this.
    a0_ftx_broadcast(&d->_stkn->_mtx.ftx);
  }
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_trylock_impl(a0_deadman_mtx_t* d) {
  if (a0_atomic_load(&d->_shutdown)) {
    A0_TSAN_HAPPENS_AFTER(&d->_shutdown);
    return A0_MAKE_SYSERR(ESHUTDOWN);
  }

  a0_err_t err = A0_MAKE_SYSERR(EBUSY);

  uint32_t tid = a0_tid();
  uint32_t old = a0_atomic_load(&d->_stkn->_mtx.ftx);
  if (!a0_ftx_tid(old)) {
    uint32_t new = (old | tid) & (~FUTEX_OWNER_DIED);
    if (a0_cas(&d->_stkn->_mtx.ftx, old, new)) {
      err = a0_ftx_owner_died(old) ? A0_MAKE_SYSERR(EOWNERDEAD) : A0_OK;
      d->_is_owner = true;
      d->_stkn->_tkn++;
      a0_ftx_broadcast(&d->_stkn->_mtx.ftx);
    }
  } else if (a0_ftx_tid(old) == tid) {
    err = A0_MAKE_SYSERR(EDEADLK);
  }

  return err;
}

a0_err_t a0_deadman_mtx_trylock(a0_deadman_mtx_t* d) {
  if (d->_is_owner) {
    return A0_OK;
  }

  a0_robust_op_start(&d->_stkn->_mtx);
  a0_err_t err = a0_deadman_mtx_trylock_impl(d);
  if (a0_mtx_lock_successful(err)) {
    __tsan_mutex_pre_lock(&d->_stkn->_mtx, 0);
    a0_robust_op_add(&d->_stkn->_mtx);
    __tsan_mutex_post_lock(&d->_stkn->_mtx, 0, 0);
  }
  a0_robust_op_end(&d->_stkn->_mtx);
  return err;
}

a0_err_t a0_deadman_mtx_lock(a0_deadman_mtx_t* d) {
  return a0_deadman_mtx_timedlock(d, A0_TIMEOUT_NEVER);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedlock_impl(a0_deadman_mtx_t* d, a0_time_mono_t* timeout) {
  a0_err_t err = A0_OK;
  while (!err || A0_SYSERR(err) == EAGAIN) {
    uint32_t old = a0_atomic_load(&d->_stkn->_mtx.ftx);
    err = a0_deadman_mtx_trylock_impl(d);
    if (!err || A0_SYSERR(err) != EBUSY) {
      return err;
    }
    err = A0_OK;

    uint32_t new = old | FUTEX_WAITERS;
    if (a0_cas(&d->_stkn->_mtx.ftx, old, new)) {
      err = a0_ftx_wait(&d->_stkn->_mtx.ftx, new, timeout);
    }
  }
  return err;
}

a0_err_t a0_deadman_mtx_timedlock(a0_deadman_mtx_t* d, a0_time_mono_t* timeout) {
  if (d->_is_owner) {
    return A0_OK;
  }

  a0_atomic_store(&d->_inop, true);

  a0_robust_op_start(&d->_stkn->_mtx);
  a0_err_t err = a0_deadman_mtx_timedlock_impl(d, timeout);
  if (a0_mtx_lock_successful(err)) {
    __tsan_mutex_pre_lock(&d->_stkn->_mtx, 0);
    a0_robust_op_add(&d->_stkn->_mtx);
    __tsan_mutex_post_lock(&d->_stkn->_mtx, 0, 0);
  }
  a0_robust_op_end(&d->_stkn->_mtx);

  a0_atomic_store(&d->_inop, false);
  return err;
}

a0_err_t a0_deadman_mtx_unlock(a0_deadman_mtx_t* d) {
  // Only the owner can unlock.
  if (!d->_is_owner) {
    return A0_MAKE_SYSERR(EPERM);
  }

  __tsan_mutex_pre_unlock(&d->_stkn->_mtx, 0);
  a0_robust_op_start(&d->_stkn->_mtx);
  a0_robust_op_del(&d->_stkn->_mtx);

  a0_atomic_and_fetch(&d->_stkn->_mtx.ftx, FUTEX_WAITERS);

  a0_robust_op_end(&d->_stkn->_mtx);
  __tsan_mutex_post_unlock(&d->_stkn->_mtx, 0);

  d->_is_owner = false;
  a0_ftx_broadcast(&d->_stkn->_mtx.ftx);
  return A0_OK;
}

a0_err_t a0_deadman_mtx_wait_locked(a0_deadman_mtx_t* d, uint64_t* out_tkn) {
  return a0_deadman_mtx_timedwait_locked(d, A0_TIMEOUT_NEVER, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedwait_locked_impl(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  uint32_t old = a0_atomic_load(&d->_stkn->_mtx.ftx);
  while (!a0_ftx_tid(old) || a0_ftx_owner_died(old)) {
    if (a0_atomic_load(&d->_shutdown)) {
      A0_TSAN_HAPPENS_AFTER(&d->_shutdown);
      return A0_MAKE_SYSERR(ESHUTDOWN);
    }

    uint32_t new = old | FUTEX_WAITERS;
    if (a0_cas(&d->_stkn->_mtx.ftx, old, new)) {
      a0_err_t err = a0_ftx_wait(&d->_stkn->_mtx.ftx, new, timeout);
      if (err && A0_SYSERR(err) != EAGAIN) {
        return err;
      }
    }

    old = a0_atomic_load(&d->_stkn->_mtx.ftx);
  }

  __tsan_mutex_pre_lock(&d->_stkn->_tkn, __tsan_mutex_try_lock);
  *out_tkn = a0_atomic_load(&d->_stkn->_tkn);
  __tsan_mutex_post_lock(&d->_stkn->_tkn, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  A0_TSAN_HAPPENS_AFTER(&d->_stkn->_mtx.ftx);
  return A0_OK;
}

a0_err_t a0_deadman_mtx_timedwait_locked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  uint64_t unused_tkn;
  if (!out_tkn) {
    out_tkn = &unused_tkn;
  }

  a0_atomic_store(&d->_inop, true);
  a0_err_t err = a0_deadman_mtx_timedwait_locked_impl(d, timeout, out_tkn);
  a0_atomic_store(&d->_inop, false);
  return err;
}

a0_err_t a0_deadman_mtx_wait_unlocked(a0_deadman_mtx_t* d, uint64_t tkn) {
  return a0_deadman_mtx_timedwait_unlocked(d, A0_TIMEOUT_NEVER, tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedwait_unlocked_impl(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  uint32_t old = a0_atomic_load(&d->_stkn->_mtx.ftx);
  while (a0_ftx_tid(old) && !a0_ftx_owner_died(old) && tkn == a0_atomic_load(&d->_stkn->_tkn)) {
    if (a0_atomic_load(&d->_shutdown)) {
      A0_TSAN_HAPPENS_AFTER(&d->_shutdown);
      return A0_MAKE_SYSERR(ESHUTDOWN);
    }

    uint32_t new = old | FUTEX_WAITERS;
    if (a0_cas(&d->_stkn->_mtx.ftx, old, new)) {
      a0_err_t err = a0_ftx_wait(&d->_stkn->_mtx.ftx, new, timeout);
      if (err && A0_SYSERR(err) != EAGAIN) {
        return err;
      }
    }

    old = a0_atomic_load(&d->_stkn->_mtx.ftx);
  }
  return A0_OK;
}

a0_err_t a0_deadman_mtx_timedwait_unlocked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  a0_atomic_store(&d->_inop, true);
  a0_err_t err = a0_deadman_mtx_timedwait_unlocked_impl(d, timeout, tkn);
  a0_atomic_store(&d->_inop, false);
  return err;
}

A0_NO_TSAN
a0_err_t a0_deadman_mtx_state(a0_deadman_mtx_t* d, a0_deadman_mtx_state_t* out_state) {
  out_state->is_owner = a0_atomic_load(&d->_is_owner);
  out_state->owner_tid = a0_ftx_tid(a0_atomic_load(&d->_stkn->_mtx.ftx));
  if (a0_ftx_owner_died(out_state->owner_tid)) {
    out_state->owner_tid = 0;
  }
  out_state->is_locked = out_state->owner_tid != 0;
  out_state->tkn = out_state->is_locked ? a0_atomic_load(&d->_stkn->_tkn) : 0;
  return A0_OK;
}
