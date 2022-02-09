#include <a0/callback.h>
#include <a0/deadman.h>
#include <a0/inline.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

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
a0_err_t a0_deadman_acquire_impl(a0_deadman_t* d, uint64_t* out_tkn, a0_err_t owner_lock_status) {
  if (!a0_mtx_lock_successful(owner_lock_status)) {
    return owner_lock_status;
  }

  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  *out_tkn = d->_tkn++;
  d->_acquired = true;
  a0_cnd_broadcast(&d->_acquire_cnd, &d->_guard);
  a0_mtx_unlock(&d->_guard);

  return owner_lock_status;
}

a0_err_t a0_deadman_acquire(a0_deadman_t* d, uint64_t* out_tkn) {
  return a0_deadman_acquire_impl(d, out_tkn, a0_mtx_lock(&d->_owner_mtx));
}

a0_err_t a0_deadman_tryacquire(a0_deadman_t* d, uint64_t* out_tkn) {
  return a0_deadman_acquire_impl(d, out_tkn, a0_mtx_trylock(&d->_owner_mtx));
}

a0_err_t a0_deadman_timedacquire(a0_deadman_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  return a0_deadman_acquire_impl(d, out_tkn, a0_mtx_timedlock(&d->_owner_mtx, timeout));
}

a0_err_t a0_deadman_release(a0_deadman_t* d) {
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  d->_acquired = false;
  a0_mtx_unlock(&d->_guard);
  a0_mtx_unlock(&d->_owner_mtx);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_wait_acquired_impl(a0_deadman_t* d, a0_callback_t acquire_cnd, uint64_t* out_tkn) {
  uint64_t unused_tkn;
  if (!out_tkn) {
    out_tkn = &unused_tkn;
  }

  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));

  while (true) {
    a0_err_t lock_result = a0_mtx_trylock(&d->_owner_mtx);
    if (a0_mtx_lock_successful(lock_result)) {
      d->_acquired = false;
      a0_mtx_unlock(&d->_owner_mtx);
      a0_err_t cnd_err = a0_callback_call(acquire_cnd);
      if (cnd_err) {
        a0_mtx_unlock(&d->_guard);
        return cnd_err;
      }
    } else if (A0_SYSERR(lock_result) == EBUSY) {
      // Either the deadman is acquired or there is a race with wait_released.
      if (d->_acquired) {
        break;
      }
      // spin conflict with wait_released
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
  IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
  a0_err_t err = A0_OK;
  while (d->_tkn == tkn) {
    err = a0_mtx_trylock(&d->_owner_mtx);
    if (a0_mtx_lock_successful(err)) {
      break;
    } else if (A0_SYSERR(err) == EBUSY) {
      // another owner exists
      a0_mtx_unlock(&d->_guard);
      err = a0_mtx_timedlock(&d->_owner_mtx, timeout);
      if (A0_SYSERR(err) == ETIMEDOUT) {
        return err;
      }
      a0_mtx_unlock(&d->_owner_mtx);
      IGNORE_OWNERDEAD(a0_mtx_lock(&d->_guard));
      err = A0_OK;
    }
  }
  d->_acquired = false;
  a0_mtx_unlock(&d->_owner_mtx);
  a0_mtx_unlock(&d->_guard);
  return err;
}

a0_err_t a0_deadman_wait_released_any(a0_deadman_t* d) {
  return a0_deadman_timedwait_released_any(d, NULL);
}

a0_err_t a0_deadman_timedwait_released_any(a0_deadman_t* d, a0_time_mono_t* timeout) {
  a0_err_t err = a0_mtx_timedlock(&d->_owner_mtx, timeout);
  if (a0_mtx_lock_successful(err)) {
    d->_acquired = false;
    a0_mtx_unlock(&d->_owner_mtx);
  }
  return err;
}
