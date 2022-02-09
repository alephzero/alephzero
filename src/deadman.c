#include <a0/callback.h>
#include <a0/deadman.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/time.h>
#include <a0/unused.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "err_macro.h"

A0_STATIC_INLINE
a0_err_t IGNORE_OWNERDEAD(a0_err_t err) {
  if (a0_mtx_lock_successful(err)) {
    return A0_OK;
  }
  return err;
}

typedef struct a0_deadman_timeout_s {
  a0_deadman_t* d;
  a0_time_mono_t* timeout;
} a0_deadman_timeout_t;

A0_STATIC_INLINE
a0_err_t a0_deadman_acquire_impl(a0_deadman_t* d, a0_err_t owner_lock_status) {
  // Failed to lock. Return the error.
  if (!a0_mtx_lock_successful(owner_lock_status)) {
    return owner_lock_status;
  }

  // The lock is successful.

  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  // Update to a new unique token.
  d->_tkn++;
  // Mark the deadman as acquired.
  d->_acquired = true;
  // Wake wait_acquired calls.
  a0_cnd_broadcast(&d->_acquire_cnd, &d->_guard);
  a0_mtx_unlock(&d->_guard);

  return owner_lock_status;
}

a0_err_t a0_deadman_acquire(a0_deadman_t* d) {
  return a0_deadman_acquire_impl(d, a0_mtx_lock(&d->_owner_mtx));
}

a0_err_t a0_deadman_tryacquire(a0_deadman_t* d) {
  return a0_deadman_acquire_impl(d, a0_mtx_trylock(&d->_owner_mtx));
}

a0_err_t a0_deadman_timedacquire(a0_deadman_t* d, a0_time_mono_t* timeout) {
  return a0_deadman_acquire_impl(d, a0_mtx_timedlock(&d->_owner_mtx, timeout));
}

a0_err_t a0_deadman_release(a0_deadman_t* d) {
  // Expect the called to own the deadman (owner_mtx).

  // Guard protects the acquired flag.
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  d->_acquired = false;
  a0_mtx_unlock(&d->_guard);

  // Release the deadman.
  a0_mtx_unlock(&d->_owner_mtx);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_wait_acquired_impl(a0_deadman_t* d, a0_callback_t acquire_cnd, uint64_t* out_tkn) {
  uint64_t unused_tkn;
  if (!out_tkn) {
    out_tkn = &unused_tkn;
  }

  // Nothing protected by the guard may block!
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));

  while (true) {
    // Check if the deadman is acquired.
    // Note: The acquired flag is not enough to determine if the deadman is acquired.
    //       The owning process may have died.
    a0_err_t lock_result = a0_mtx_trylock(&d->_owner_mtx);

    if (a0_mtx_lock_successful(lock_result)) {
      // The owning process may have died. Reset the acquired flag.
      d->_acquired = false;

      // Release the deadman and wait on the acquire condition.
      a0_mtx_unlock(&d->_owner_mtx);
      a0_err_t cnd_err = a0_callback_call(acquire_cnd);
      if (cnd_err) {
        a0_mtx_unlock(&d->_guard);
        return cnd_err;
      }
    } else if (d->_acquired) {
      // The deadman is acquired.
      break;
    } else {
      // There was a race with wait_released.
      // This is a very short period. Spin.
      a0_mtx_unlock(&d->_guard);
      IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
    }
  }

  *out_tkn = d->_tkn;
  a0_mtx_unlock(&d->_guard);
  return A0_OK;
}

a0_err_t a0_deadman_wait_acquired(a0_deadman_t* d, uint64_t* out_tkn) {
  return a0_deadman_timedwait_acquired(d, NULL, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_wait_timedacquired_cnd(void* user_data) {
  a0_deadman_timeout_t* deadman_timeout = (a0_deadman_timeout_t*)user_data;
  return a0_cnd_timedwait(
      &deadman_timeout->d->_acquire_cnd,
      &deadman_timeout->d->_guard,
      deadman_timeout->timeout);
}

a0_err_t a0_deadman_timedwait_acquired(a0_deadman_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  a0_deadman_timeout_t deadman_timeout = {d, timeout};
  return a0_deadman_wait_acquired_impl(d, (a0_callback_t){&deadman_timeout, a0_deadman_wait_timedacquired_cnd}, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_isacquired_cnd(void* user_data) {
  A0_MAYBE_UNUSED(user_data);
  return A0_MAKE_SYSERR(EBUSY);
}

a0_err_t a0_deadman_isacquired(a0_deadman_t* d, bool* isacquired, uint64_t* out_tkn) {
  bool unused;
  if (!isacquired) {
    isacquired = &unused;
  }

  a0_err_t err = a0_deadman_wait_acquired_impl(d, (a0_callback_t){NULL, a0_deadman_isacquired_cnd}, out_tkn);
  *isacquired = !err;
  if (A0_SYSERR(err) == EBUSY) {
    err = A0_OK;
  }
  return err;
}

a0_err_t a0_deadman_wait_released(a0_deadman_t* d, uint64_t tkn) {
  return a0_deadman_timedwait_released(d, NULL, tkn);
}

a0_err_t a0_deadman_timedwait_released(a0_deadman_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  // Nothing protected by the guard may block!
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  a0_err_t err = A0_OK;
  while (d->_tkn == tkn) {
    // Check if the deadman is acquired.
    err = a0_mtx_trylock(&d->_owner_mtx);
    if (a0_mtx_lock_successful(err)) {
      // If not, we're done
      break;
    } else if (A0_SYSERR(err) == EBUSY) {
      // The deadman is acquired.
      //
      // Locking the owner_mtx (without guard) will block until the owner
      // releases or dies.
      //
      // wait_released owns the owner_mtx for a very short time.
      // wait_acquired has special logic to detect that this is not an owner.
      a0_mtx_unlock(&d->_guard);
      err = a0_mtx_timedlock(&d->_owner_mtx, timeout);
      if (A0_SYSERR(err) == ETIMEDOUT) {
        return err;
      }
      a0_mtx_unlock(&d->_owner_mtx);
      IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));

      // Reset err and try again.
      err = A0_OK;
    }
  }

  // The owner_mtx is locked here, but the deadman is not acquired.
  d->_acquired = false;

  a0_mtx_unlock(&d->_owner_mtx);
  a0_mtx_unlock(&d->_guard);
  return err;
}
