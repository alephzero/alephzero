#include <a0/callback.h>
#include <a0/deadman_mtx.h>
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

typedef struct a0_deadman_mtx_timeout_s {
  a0_deadman_mtx_t* d;
  a0_time_mono_t* timeout;
} a0_deadman_mtx_timeout_t;

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_lock_impl(a0_deadman_mtx_t* d, a0_err_t owner_lock_status) {
  // Failed to lock. Return the error.
  if (!a0_mtx_lock_successful(owner_lock_status)) {
    return owner_lock_status;
  }

  // The lock is successful.

  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  // Update to a new unique token.
  d->_tkn++;
  // Mark the deadman as locked.
  d->_locked = true;
  // Wake wait_locked calls.
  a0_cnd_broadcast(&d->_lock_cnd, &d->_guard);
  a0_mtx_unlock(&d->_guard);

  return owner_lock_status;
}

a0_err_t a0_deadman_mtx_lock(a0_deadman_mtx_t* d) {
  return a0_deadman_mtx_lock_impl(d, a0_mtx_lock(&d->_owner_mtx));
}

a0_err_t a0_deadman_mtx_trylock(a0_deadman_mtx_t* d) {
  return a0_deadman_mtx_lock_impl(d, a0_mtx_trylock(&d->_owner_mtx));
}

a0_err_t a0_deadman_mtx_timedlock(a0_deadman_mtx_t* d, a0_time_mono_t* timeout) {
  return a0_deadman_mtx_lock_impl(d, a0_mtx_timedlock(&d->_owner_mtx, timeout));
}

a0_err_t a0_deadman_mtx_unlock(a0_deadman_mtx_t* d) {
  // Expect the called to own the deadman (owner_mtx).

  // Guard protects the locked flag.
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  d->_locked = false;
  a0_mtx_unlock(&d->_guard);

  // Release the deadman.
  a0_mtx_unlock(&d->_owner_mtx);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_wait_locked_impl(a0_deadman_mtx_t* d, a0_callback_t lock_cnd, uint64_t* out_tkn) {
  uint64_t unused_tkn;
  if (!out_tkn) {
    out_tkn = &unused_tkn;
  }

  while (true) {
    // Nothing protected by the guard may block!
    IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));

    // Check if the deadman is locked.
    // Note: The locked flag is not enough to determine if the deadman is locked.
    //       The owning process may have died.
    if (a0_mtx_lock_successful(a0_mtx_trylock(&d->_owner_mtx))) {
      // The owning process may have died. Reset the locked flag.
      d->_locked = false;

      // Release the deadman and wait on the lock condition.
      a0_mtx_unlock(&d->_owner_mtx);
      a0_err_t cnd_err = a0_callback_call(lock_cnd);
      if (cnd_err) {
        a0_mtx_unlock(&d->_guard);
        return cnd_err;
      }
    } else if (d->_locked) {
      // The deadman is locked.
      *out_tkn = d->_tkn;
      a0_mtx_unlock(&d->_guard);
      return A0_OK;
    }

    a0_mtx_unlock(&d->_guard);
  }
}

a0_err_t a0_deadman_mtx_wait_locked(a0_deadman_mtx_t* d, uint64_t* out_tkn) {
  return a0_deadman_mtx_timedwait_locked(d, NULL, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_timedwait_locked_cnd(void* user_data) {
  a0_deadman_mtx_timeout_t* deadman_timeout = (a0_deadman_mtx_timeout_t*)user_data;
  return a0_cnd_timedwait(
      &deadman_timeout->d->_lock_cnd,
      &deadman_timeout->d->_guard,
      deadman_timeout->timeout);
}

a0_err_t a0_deadman_mtx_timedwait_locked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  a0_deadman_mtx_timeout_t deadman_timeout = {d, timeout};
  return a0_deadman_mtx_wait_locked_impl(d, (a0_callback_t){&deadman_timeout, a0_deadman_mtx_timedwait_locked_cnd}, out_tkn);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_mtx_query_cnd(void* user_data) {
  A0_MAYBE_UNUSED(user_data);
  return A0_MAKE_SYSERR(EBUSY);
}

a0_err_t a0_deadman_mtx_state(a0_deadman_mtx_t* d, bool* out_islocked, uint64_t* out_tkn) {
  bool unused;
  if (!out_islocked) {
    out_islocked = &unused;
  }

  a0_err_t err = a0_deadman_mtx_wait_locked_impl(d, (a0_callback_t){NULL, a0_deadman_mtx_query_cnd}, out_tkn);
  *out_islocked = !err;
  if (A0_SYSERR(err) == EBUSY) {
    err = A0_OK;
  }
  return err;
}

a0_err_t a0_deadman_mtx_wait_unlocked(a0_deadman_mtx_t* d, uint64_t tkn) {
  return a0_deadman_mtx_timedwait_unlocked(d, NULL, tkn);
}

a0_err_t a0_deadman_mtx_timedwait_unlocked(a0_deadman_mtx_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  // Nothing protected by the guard may block!
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  a0_err_t err = A0_OK;
  while (d->_tkn == tkn) {
    // Check if the deadman is locked.
    err = a0_mtx_trylock(&d->_owner_mtx);
    if (a0_mtx_lock_successful(err)) {
      // If not, we're done
      break;
    }

    // The deadman is locked.
    //
    // Locking the owner_mtx (without guard) will block until the owner
    // unlocks or dies.
    //
    // wait_unlocked owns the owner_mtx for a very short time.
    // _locked is set to false to differentiate between the lock and wait_unlocked.
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

  // The owner_mtx is locked here, but the deadman_mtx is not locked.
  d->_locked = false;

  a0_mtx_unlock(&d->_owner_mtx);
  a0_mtx_unlock(&d->_guard);
  return err;
}
