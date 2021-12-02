#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/rwmtx.h>
#include <a0/time.h>

#include <errno.h>
#include <stdio.h>

#include "err_macro.h"

// Lock a mutex, ignoring whether the owner died.
A0_STATIC_INLINE
a0_err_t lock_consistent(a0_mtx_t* mtx) {
  a0_err_t err = a0_mtx_lock(mtx);
  if (A0_SYSERR(err) == EOWNERDEAD) {
    err = a0_mtx_consistent(mtx);
  }
  return err;
}

// Try locking a mutex, ignoring whether the owner died.
A0_STATIC_INLINE
a0_err_t trylock_consistent(a0_mtx_t* mtx) {
  a0_err_t err = a0_mtx_trylock(mtx);
  if (A0_SYSERR(err) == EOWNERDEAD) {
    err = a0_mtx_consistent(mtx);
  }
  return err;
}

// Lock a given mutex, relaxing the guard to let another thread release the lock in question.
A0_STATIC_INLINE
a0_err_t guarded_lock(a0_mtx_t* guard, a0_cnd_t* cnd, a0_mtx_t* mtx) {
  a0_err_t err;
  while (A0_SYSERR(err = trylock_consistent(mtx)) == EBUSY) {
    a0_cnd_wait(cnd, guard);
  }
  return err;
}

// Try locking a given mutex for no more than the given timeout.
// The guard is relaxed to let another thread release the lock in question.
A0_STATIC_INLINE
a0_err_t guarded_timedlock(a0_mtx_t* guard, a0_cnd_t* cnd, a0_mtx_t* mtx, a0_time_mono_t timeout) {
  a0_err_t err;
  while (A0_SYSERR(err = trylock_consistent(mtx)) == EBUSY) {
    A0_RETURN_ERR_ON_ERR(a0_cnd_timedwait(cnd, guard, timeout));
  }
  return err;
}

// Common implementation component for rlock operations.
// Requires the guard to be locked.
A0_STATIC_INLINE
a0_err_t a0_rwmtx_tryrlock_impl(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_rwmtx_tkn_t* tkn) {
  // If there are more slots than used, grab a known free slot.
  // This happens if there have been fewer readers than slots, since the last writer-lock was acquired.
  if (rwmtx->_next_rmtx_idx < rmtx_span.size) {
    a0_mtx_t* rmtx = &rmtx_span.arr[rwmtx->_next_rmtx_idx++];
    lock_consistent(rmtx);
    tkn->mtx = rmtx;
    return A0_OK;
  }

  // All slots have been filled, at some point since the last writer-lock was acquired.
  // Check if any have been released.
  for (size_t i = 0; i < rmtx_span.size; i++) {
    a0_mtx_t* rmtx = &rmtx_span.arr[i];
    if (!trylock_consistent(rmtx)) {
      tkn->mtx = rmtx;
      return A0_OK;
    }
  }

  // All reader slots are full.
  return A0_MAKE_SYSERR(EBUSY);
}

a0_err_t a0_rwmtx_tryrlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_rwmtx_tkn_t* tkn) {
  lock_consistent(&rwmtx->guard);

  // Try to grab and release the writer mutex.
  // Failing indicates a writer is active and this try-rlock fails.
  a0_err_t err = trylock_consistent(&rwmtx->wmtx);
  if (!err) {
    a0_mtx_unlock(&rwmtx->wmtx);
    // Try to grab an available reader slot.
    err = a0_rwmtx_tryrlock_impl(rwmtx, rmtx_span, tkn);
  }

  a0_mtx_unlock(&rwmtx->guard);
  return err;
}

a0_err_t a0_rwmtx_rlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_rwmtx_tkn_t* tkn) {
  lock_consistent(&rwmtx->guard);

  // Try rlock, until success.
  while (true) {
    // Block until the writer-lock is available.
    // Release the guard to allow the writer to unlock.
    //
    // Do not hold the writer-lock across attempts, or it might starve wlock.
    guarded_lock(&rwmtx->guard, &rwmtx->cnd, &rwmtx->wmtx);
    a0_mtx_unlock(&rwmtx->wmtx);

    // Check if a reader-slot is available.
    if (!a0_rwmtx_tryrlock_impl(rwmtx, rmtx_span, tkn)) {
      break;
    }
    // No reader-slot is available at this time.
    // Sleep until an unlock event and try again.
    a0_cnd_wait(&rwmtx->cnd, &rwmtx->guard);
  }

  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}

a0_err_t a0_rwmtx_timedrlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_time_mono_t timeout, a0_rwmtx_tkn_t* tkn) {
  a0_err_t err;
  lock_consistent(&rwmtx->guard);

  // Try rlock, until success or timeout.
  while (true) {
    // Block until the writer-lock is available, or the timeout is exceeded.
    // Release the guard to allow the writer to unlock.
    //
    // Do not hold the writer-lock across attempts, or it might starve wlock.
    if ((err = guarded_timedlock(&rwmtx->guard, &rwmtx->cnd, &rwmtx->wmtx, timeout)) != A0_OK) {
      a0_mtx_unlock(&rwmtx->guard);
      return err;
    }
    a0_mtx_unlock(&rwmtx->wmtx);

    // Check if a reader-slot is available.
    if (!a0_rwmtx_tryrlock_impl(rwmtx, rmtx_span, tkn)) {
      break;
    }
    // No reader-slot is available at this time.
    // Sleep until an unlock event, or the timeout is exceeded, and try again.
    if ((err = a0_cnd_timedwait(&rwmtx->cnd, &rwmtx->guard, timeout)) != A0_OK) {
      a0_mtx_unlock(&rwmtx->guard);
      return err;
    }
  }

  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}

a0_err_t a0_rwmtx_trywlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_rwmtx_tkn_t* tkn) {
  lock_consistent(&rwmtx->guard);

  // Try to grab the writer-lock.
  a0_err_t err = trylock_consistent(&rwmtx->wmtx);
  if (err) {
    a0_mtx_unlock(&rwmtx->guard);
    return err;
  }

  // Check whether there are any active readers.
  for (; rwmtx->_next_rmtx_idx; rwmtx->_next_rmtx_idx--) {
    a0_mtx_t* rmtx = &rmtx_span.arr[rwmtx->_next_rmtx_idx - 1];
    err = trylock_consistent(rmtx);
    if (err) {
      a0_mtx_unlock(&rwmtx->wmtx);
      a0_mtx_unlock(&rwmtx->guard);
      return err;
    }
    a0_mtx_unlock(rmtx);
  }

  tkn->mtx = &rwmtx->wmtx;

  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}

a0_err_t a0_rwmtx_wlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_rwmtx_tkn_t* tkn) {
  lock_consistent(&rwmtx->guard);

  // Grab the writer-lock. Wait if necessary.
  a0_err_t err = guarded_lock(&rwmtx->guard, &rwmtx->cnd, &rwmtx->wmtx);
  if (err) {
    a0_mtx_unlock(&rwmtx->guard);
    return err;
  }

  // Validate all reader slots are empty. If a slot is not empty, wait until it is.
  for (; rwmtx->_next_rmtx_idx; rwmtx->_next_rmtx_idx--) {
    a0_mtx_t* rmtx = &rmtx_span.arr[rwmtx->_next_rmtx_idx - 1];
    guarded_lock(&rwmtx->guard, &rwmtx->cnd, rmtx);
    a0_mtx_unlock(rmtx);
  }

  tkn->mtx = &rwmtx->wmtx;

  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}

a0_err_t a0_rwmtx_timedwlock(a0_rwmtx_t* rwmtx, a0_rwmtx_rmtx_span_t rmtx_span, a0_time_mono_t timeout, a0_rwmtx_tkn_t* tkn) {
  lock_consistent(&rwmtx->guard);

  // Grab the writer-lock. Wait if necessary, but not to exceed the timeout.
  a0_err_t err = guarded_timedlock(&rwmtx->guard, &rwmtx->cnd, &rwmtx->wmtx, timeout);
  if (err) {
    a0_mtx_unlock(&rwmtx->guard);
    return err;
  }

  // Validate all reader slots are empty. If a slot is not empty, wait until it is.
  // If the timeout is exceeded while waiting, the writer-lock is released.
  for (; rwmtx->_next_rmtx_idx; rwmtx->_next_rmtx_idx--) {
    a0_mtx_t* rmtx = &rmtx_span.arr[rwmtx->_next_rmtx_idx - 1];
    err = guarded_timedlock(&rwmtx->guard, &rwmtx->cnd, rmtx, timeout);
    if (err) {
      a0_mtx_unlock(&rwmtx->wmtx);
      a0_mtx_unlock(&rwmtx->guard);
      return err;
    }
    a0_mtx_unlock(rmtx);
  }

  tkn->mtx = &rwmtx->wmtx;

  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}

a0_err_t a0_rwmtx_unlock(a0_rwmtx_t* rwmtx, a0_rwmtx_tkn_t tkn) {
  // The mutex is released outside the guard to avoid mutex inversion.
  a0_mtx_unlock(tkn.mtx);

  // Notify that a lock slot is available.
  lock_consistent(&rwmtx->guard);
  a0_cnd_broadcast(&rwmtx->cnd, &rwmtx->guard);
  a0_mtx_unlock(&rwmtx->guard);
  return A0_OK;
}
