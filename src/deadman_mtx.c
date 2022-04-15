#include <a0/deadman_mtx.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/tid.h>
#include <a0/time.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "atomic.h"
#include "err_macro.h"
#include "ftx.h"
#include "robust.h"

a0_err_t a0_deadman_mtx_init(a0_deadman_mtx_t* d, a0_deadman_mtx_shared_token_t* shared_token) {
  *d = (a0_deadman_mtx_t)A0_EMPTY;
  d->_stkn = shared_token;
  return A0_OK;
}

a0_err_t a0_deadman_mtx_shutdown(a0_deadman_mtx_t* d) {
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
    return A0_MAKE_SYSERR(ESHUTDOWN);
  }

  a0_err_t err = A0_MAKE_SYSERR(EBUSY);

  const uint32_t tid = a0_tid();
  uint32_t old = a0_cas_val(&d->_stkn->_mtx.ftx, 0, tid);
  if (!old) {
    err = A0_OK;
  } else if (a0_ftx_owner_died(old) && a0_cas(&d->_stkn->_mtx.ftx, old, tid)) {
    err = A0_MAKE_SYSERR(EOWNERDEAD);
  } else if (a0_ftx_tid(old) == tid) {
    err = A0_MAKE_SYSERR(EDEADLK);
  }

  if (a0_mtx_lock_successful(err)) {
    d->_is_owner = true;
    d->_stkn->_tkn++;
    a0_ftx_broadcast(&d->_stkn->_mtx.ftx);
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
    a0_robust_op_add(&d->_stkn->_mtx);
  }
  a0_robust_op_end(&d->_stkn->_mtx);
  return err;
}

a0_err_t a0_deadman_mtx_lock(a0_deadman_mtx_t* d) {
  return a0_deadman_mtx_timedlock(d, NULL);
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
    err = a0_ftx_wait(&d->_stkn->_mtx.ftx, old, timeout);
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
    a0_robust_op_add(&d->_stkn->_mtx);
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

  a0_robust_op_start(&d->_stkn->_mtx);
  a0_robust_op_del(&d->_stkn->_mtx);

  a0_atomic_store(&d->_stkn->_mtx.ftx, 0);

  a0_robust_op_end(&d->_stkn->_mtx);

  d->_is_owner = false;
  a0_ftx_broadcast(&d->_stkn->_mtx.ftx);
  return A0_OK;
}

a0_err_t a0_deadman_mtx_wait_locked(a0_deadman_mtx_t* d, uint64_t* out_tkn) {
  return a0_deadman_mtx_timedwait_locked(d, NULL, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedwait_locked_impl(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  uint32_t val = a0_atomic_load(&d->_stkn->_mtx.ftx);
  while (!val || a0_ftx_owner_died(val)) {
    if (a0_atomic_load(&d->_shutdown)) {
      return A0_MAKE_SYSERR(ESHUTDOWN);
    }

    a0_err_t err = a0_ftx_wait(&d->_stkn->_mtx.ftx, val, timeout);
    if (err && A0_SYSERR(err) != EAGAIN) {
      return err;
    }

    val = a0_atomic_load(&d->_stkn->_mtx.ftx);
  }

  *out_tkn = d->_stkn->_tkn;
  return A0_OK;
}

a0_err_t a0_deadman_mtx_timedwait_locked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  uint64_t unused_tkn;
  if (out_tkn == NULL) {
    out_tkn = &unused_tkn;
  }

  a0_atomic_store(&d->_inop, true);
  a0_err_t err = a0_deadman_mtx_timedwait_locked_impl(d, timeout, out_tkn);
  a0_atomic_store(&d->_inop, false);
  return err;
}

a0_err_t a0_deadman_mtx_wait_unlocked(a0_deadman_mtx_t* d, uint64_t tkn) {
  return a0_deadman_mtx_timedwait_unlocked(d, NULL, tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedwait_unlocked_impl(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  uint32_t val = a0_atomic_load(&d->_stkn->_mtx.ftx);
  while (val && !a0_ftx_owner_died(val) && tkn == a0_atomic_load(&d->_stkn->_tkn)) {
    if (a0_atomic_load(&d->_shutdown)) {
      return A0_MAKE_SYSERR(ESHUTDOWN);
    }

    a0_err_t err = a0_ftx_wait(&d->_stkn->_mtx.ftx, val, timeout);
    if (err && A0_SYSERR(err) != EAGAIN) {
      return err;
    }

    val = a0_atomic_load(&d->_stkn->_mtx.ftx);
  }
  return A0_OK;
}

a0_err_t a0_deadman_mtx_timedwait_unlocked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  a0_atomic_store(&d->_inop, true);
  a0_err_t err = a0_deadman_mtx_timedwait_unlocked_impl(d, timeout, tkn);
  a0_atomic_store(&d->_inop, false);
  return err;
}

a0_err_t a0_deadman_mtx_state(a0_deadman_mtx_t* d, a0_deadman_mtx_state_t* out_state) {
  out_state->is_owner = d->_is_owner;
  out_state->owner_tid = a0_atomic_load(&d->_stkn->_mtx.ftx);
  if (a0_ftx_owner_died(out_state->owner_tid)) {
    out_state->owner_tid = 0;
  }
  out_state->is_locked = out_state->owner_tid != 0;
  out_state->tkn = out_state->is_locked ? a0_atomic_load(&d->_stkn->_tkn) : 0;
  return A0_OK;
}
