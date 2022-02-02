#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/time.h>

#include <errno.h>
#include <stdio.h>

#include "err_macro.h"

A0_STATIC_INLINE
bool was_acquired(a0_err_t err) {
  return !err || A0_SYSERR(err) == EOWNERDEAD;
}

A0_STATIC_INLINE
a0_err_t notify_on_acquire(a0_deadman_t* deadman, a0_err_t lock_err) {
  if (was_acquired(lock_err)) {
    a0_mtx_lock(&deadman->_mtx);
    a0_cnd_broadcast(&deadman->_cnd, &deadman->_mtx);
    a0_mtx_unlock(&deadman->_mtx);
  }
  return lock_err;
}

a0_err_t a0_deadman_acquire(a0_deadman_t* deadman) {
  // return notify_on_acquire(deadman, a0_rwmtx_wlock(&deadman->_rwmtx));

  a0_mtx_lock(&deadman->_mtx);
  a0_err_t err;
  while (A0_SYSERR(err = a0_rwmtx_trywlock(deadman->_rwmtx)) == EBUSY) {
    a0_cnd_wait(&deadman->_cnd, &deadman->_mtx);
  }
  a0_mtx_unlock(&deadman->_mtx);
}

a0_err_t a0_deadman_tryacquire(a0_deadman_t* deadman) {
  // This may be blocked by an isactive check. Do we want to protect this with _guard?
  // return notify_on_acquire(deadman, a0_rwmtx_trylock(&deadman->_rwmtx));

  a0_mtx_lock(&deadman->_mtx);
  a0_err_t err = a0_rwmtx_trywlock(deadman->_rwmtx);
  a0_mtx_unlock(&deadman->_mtx);
}

a0_err_t a0_deadman_timedacquire(a0_deadman_t* deadman, a0_time_mono_t timeout) {
  return notify_on_acquire(deadman, a0_mtx_timedlock(&deadman->_mtx, timeout));
}

a0_err_t a0_deadman_release(a0_deadman_t* deadman) {
  a0_rwmtx_unlock(&deadman->_rwmtx);
  // a0_mtx_lock(&deadman->_guard);
  // deadman->_acquired = false;
  // a0_mtx_unlock(deadman->_mtx);
  // a0_cnd_broadcast(&deadman->_cnd, &deadman->_mtx);
  // a0_mtx_unlock(&deadman->_guard);

  // // Notify that a lock slot is available.
  // a0_mtx_lock(&deadman->_guard);
  // a0_cnd_broadcast(&deadman->_cnd, &deadman->_guard);
  // a0_mtx_unlock(&deadman->_guard);
  return A0_OK;
}

a0_err_t a0_deadman_isactive(a0_deadman_t* deadman, bool* isactive) {
  a0_err_t err = a0_rwmtx_tryrlock(&deadman->_rwmtx);
  *isactive = !was_acquired(err);
  if (!*isactive) {
    a0_rwmtx_unlock(&deadman->_rwmtx);
  }
  // a0_mtx_lock(&deadman->_guard);
  // a0_err_t err = a0_mtx_trylock(&deadman->_mtx);
  // *isactive = !was_acquired(err);
  // if (!*isactive) {
  //   a0_mtx_unlock(&deadman->_mtx);
  // }
  // a0_mtx_unlock(&deadman->_guard);
  return A0_OK;
}

a0_err_t a0_deadman_wait_active(a0_deadman_t* deadman) {
  a0_mtx_lock(&deadman->_mtx);
  while (was_acquired(a0_rwmtx_tryrlock(&deadman->_rwmtx))) {
    a0_rwmtx_unlock(&deadman->_rwmtx);
    a0_cnd_wait(&deadman->_cnd, &deadman->_mtx);
  }
  a0_mtx_unlock(&deadman->_mtx);
  return A0_OK;
}

a0_err_t a0_deadman_wait_released(a0_deadman_t* deadman) {
  a0_err_t err = a0_rwmtx_rlock(&deadman->_rwmtx);
  a0_rwmtx_unlock(&deadman->_rwmtx);
  return err;
}

a0_err_t a0_deadman_timedwait_active(a0_deadman_t* deadman, a0_time_mono_t timeout) {
  // TODO
}

a0_err_t a0_deadman_timedwait_released(a0_deadman_t* deadman, a0_time_mono_t timeout) {
  // There is a race condition here. The deadman will appear active for a short time, until released.
  // Can this be wrapped with _guard without holding guard for a long time?
  a0_err_t err = a0_mtx_timedlock(&deadman->_mtx, timeout);
  if (was_acquired(err)) {
    a0_mtx_unlock(&deadman->_mtx);
  }
  return err;
}
