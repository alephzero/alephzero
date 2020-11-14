#include "mtx.h"

#include <a0/errno.h>

#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <syscall.h>
#include <unistd.h>

#include "atomic.h"
#include "macros.h"

const unsigned __tsan_mutex_linker_init = 1 << 0;
const unsigned __tsan_mutex_write_reentrant = 1 << 1;
const unsigned __tsan_mutex_read_reentrant = 1 << 2;
const unsigned __tsan_mutex_not_static = 1 << 8;
const unsigned __tsan_mutex_read_lock = 1 << 3;
const unsigned __tsan_mutex_try_lock = 1 << 4;
const unsigned __tsan_mutex_try_lock_failed = 1 << 5;
const unsigned __tsan_mutex_recursive_lock = 1 << 6;
const unsigned __tsan_mutex_recursive_unlock = 1 << 7;

#ifdef A0_TSAN_ENABLED

void __tsan_mutex_create(void* addr, unsigned flags);
void __tsan_mutex_destroy(void* addr, unsigned flags);
void __tsan_mutex_pre_lock(void* addr, unsigned flags);
void __tsan_mutex_post_lock(void* addr, unsigned flags, int recursion);
int __tsan_mutex_pre_unlock(void* addr, unsigned flags);
void __tsan_mutex_post_unlock(void* addr, unsigned flags);
void __tsan_mutex_pre_signal(void* addr, unsigned flags);
void __tsan_mutex_post_signal(void* addr, unsigned flags);
void __tsan_mutex_pre_divert(void* addr, unsigned flags);
void __tsan_mutex_post_divert(void* addr, unsigned flags);

#else

#define _U_ __attribute__((unused))

A0_STATIC_INLINE void _U_ __tsan_mutex_create(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_destroy(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_pre_lock(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_post_lock(_U_ void* addr,
                                                 _U_ unsigned flags,
                                                 _U_ int recursion) {}
A0_STATIC_INLINE int _U_ __tsan_mutex_pre_unlock(_U_ void* addr, _U_ unsigned flags) {
  return 0;
}
A0_STATIC_INLINE void _U_ __tsan_mutex_post_unlock(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_pre_signal(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_post_signal(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_pre_divert(_U_ void* addr, _U_ unsigned flags) {}
A0_STATIC_INLINE void _U_ __tsan_mutex_post_divert(_U_ void* addr, _U_ unsigned flags) {}

#endif

typedef struct robust_list robust_list_t;
_Thread_local struct robust_list_head a0_robust_head;

A0_STATIC_INLINE
void robust_init() {
  a0_robust_head.list.next = &a0_robust_head.list;
  a0_robust_head.futex_offset = offsetof(a0_mtx_t, ftx);
  a0_robust_head.list_op_pending = NULL;
  syscall(SYS_set_robust_list, &a0_robust_head.list, sizeof(a0_robust_head));
}

A0_STATIC_INLINE
bool robust_is_head(a0_mtx_t* mtx) {
  return mtx == (a0_mtx_t*)&a0_robust_head;
}

A0_STATIC_INLINE
void robust_op_start(a0_mtx_t* mtx) {
  a0_robust_head.list_op_pending = (struct robust_list*)mtx;
  a0_barrier();
}

A0_STATIC_INLINE
void robust_op_end(a0_mtx_t* mtx) {
  (void)mtx;
  a0_barrier();
  a0_robust_head.list_op_pending = NULL;
}

A0_STATIC_INLINE
void robust_op_add(a0_mtx_t* mtx) {
  a0_mtx_t* old_first = (a0_mtx_t*)a0_robust_head.list.next;

  mtx->prev = (a0_mtx_t*)&a0_robust_head;
  mtx->next = old_first;

  a0_barrier();

  a0_robust_head.list.next = (robust_list_t*)mtx;
  if (!robust_is_head(old_first)) {
    old_first->prev = mtx;
  }
}

A0_STATIC_INLINE
void robust_op_del(a0_mtx_t* mtx) {
  a0_mtx_t* prev = mtx->prev;
  a0_mtx_t* next = mtx->next;
  prev->next = next;
  if (!robust_is_head(next)) {
    next->prev = prev;
  }
}

_Thread_local uint32_t a0_tid_ = 0;

A0_STATIC_INLINE
uint32_t tid_load() {
  return syscall(SYS_gettid);
}

A0_STATIC_INLINE
void tid_reset() {
  a0_tid_ = 0;
}

A0_STATIC_INLINE
void tid_reset_atfork() {
  pthread_atfork(NULL, NULL, &tid_reset);
}

static pthread_once_t tid_reset_atfork_once;

A0_STATIC_INLINE
void init_thread() {
  a0_tid_ = tid_load();
  pthread_once(&tid_reset_atfork_once, tid_reset_atfork);
  robust_init();
}

A0_STATIC_INLINE
uint32_t a0_tid() {
  if (A0_UNLIKELY(!a0_tid_)) {
    init_thread();
  }
  return a0_tid_;
}

A0_STATIC_INLINE
uint32_t ftx_tid(a0_ftx_t ftx) {
  return ftx & FUTEX_TID_MASK;
}

A0_STATIC_INLINE
bool ftx_owner_died(a0_ftx_t ftx) {
  return ftx & FUTEX_OWNER_DIED;
}

static const uint32_t FTX_NOTRECOVERABLE = FUTEX_TID_MASK | FUTEX_OWNER_DIED;

A0_STATIC_INLINE
bool ftx_notrecoverable(a0_ftx_t ftx) {
  return (ftx & FTX_NOTRECOVERABLE) == FTX_NOTRECOVERABLE;
}

A0_STATIC_INLINE
errno_t a0_mtx_timedlock_impl(a0_mtx_t* mtx, const timespec_t* timeout) {
  const uint32_t tid = a0_tid();

  errno_t err = EINTR;
  while (err == EINTR) {
    const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_ACQUIRE);
    if (ftx_notrecoverable(val)) {
      return ENOTRECOVERABLE;
    }

    // If the atomic 0->TID transition fails.
    if (__sync_bool_compare_and_swap(&mtx->ftx, 0, tid)) {
      // Fastpath succeeded, so no need to call into the kernel.
      // Because this is the fastpath, it's a good idea to avoid even having to
      // load the value again down below.
      return A0_OK;
    }

    // Wait in the kernel, which handles atomically ORing in FUTEX_WAITERS
    // before actually sleeping.
    err = a0_ftx_lock_pi(&mtx->ftx, timeout);
  }

  if (!err) {
    const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_ACQUIRE);
    return ftx_owner_died(val) ? EOWNERDEAD : A0_OK;
  }

  return err;
}

errno_t a0_mtx_timedlock(a0_mtx_t* mtx, const timespec_t* timeout) {
  __tsan_mutex_pre_lock(mtx, 0);
  robust_op_start(mtx);
  const errno_t err = a0_mtx_timedlock_impl(mtx, timeout);
  if (!err || err == EOWNERDEAD) {
    robust_op_add(mtx);
  }
  robust_op_end(mtx);
  __tsan_mutex_post_lock(mtx, 0, 0);
  return err;
}

errno_t a0_mtx_lock(a0_mtx_t* mtx) {
  return a0_mtx_timedlock(mtx, NULL);
}

A0_STATIC_INLINE
errno_t a0_mtx_trylock_impl(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  // Try an atomic 0->TID transition.
  uint32_t old = __sync_val_compare_and_swap(&mtx->ftx, 0, tid);

  if (!old) {
    robust_op_add(mtx);
    return A0_OK;
  }

  if (ftx_notrecoverable(old)) {
    return ENOTRECOVERABLE;
  }

  if (!ftx_owner_died(old)) {
    // Somebody else had it locked; we failed.
    return EBUSY;
  }

  // FUTEX_OWNER_DIED was set, so we have to call into the kernel to deal
  // with resetting it.
  errno_t err = a0_ftx_trylock_pi(&mtx->ftx);
  if (!err) {
    robust_op_add(mtx);
    const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_ACQUIRE);
    return ftx_owner_died(val) ? EOWNERDEAD : A0_OK;
  }

  // EWOULDBLOCK means that somebody else beat us to it.
  if (err == EWOULDBLOCK) {
    return EBUSY;
  }

  return ENOTRECOVERABLE;
}

errno_t a0_mtx_trylock(a0_mtx_t* mtx) {
  __tsan_mutex_pre_lock(mtx, __tsan_mutex_try_lock);
  robust_op_start(mtx);
  errno_t err = a0_mtx_trylock_impl(mtx);
  robust_op_end(mtx);
  if (!err || err == EOWNERDEAD) {
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock, 0);
  } else {
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  }
  return err;
}

errno_t a0_mtx_consistent(a0_mtx_t* mtx) {
  const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_ACQUIRE);

  if (!ftx_owner_died(val)) {
    return EINVAL;
  }

  if (ftx_tid(val) != a0_tid()) {
    return EPERM;
  }

  __atomic_and_fetch(&mtx->ftx, ~FUTEX_OWNER_DIED, __ATOMIC_RELAXED);

  return A0_OK;
}

errno_t a0_mtx_unlock(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_SEQ_CST);

  if (ftx_tid(val) != tid) {
    return EPERM;
  }

  __tsan_mutex_pre_unlock(mtx, 0);

  uint32_t new_val = 0;
  if (ftx_owner_died(val)) {
    new_val = FTX_NOTRECOVERABLE;
  }

  robust_op_start(mtx);
  robust_op_del(mtx);

  // If the atomic TID->0 transition fails (ie FUTEX_WAITERS is set),
  // There aren't any waiters, so no need to call into the kernel.
  if (!__sync_bool_compare_and_swap(&mtx->ftx, tid, new_val)) {
    // The kernel handles everything else.
    a0_ftx_unlock_pi(&mtx->ftx);
    if (new_val) {
      __atomic_or_fetch(&mtx->ftx, new_val, __ATOMIC_RELAXED);
    }
  }

  robust_op_end(mtx);
  __tsan_mutex_post_unlock(mtx, 0);

  return A0_OK;
}

// TODO: Handle ENOTRECOVERABLE
errno_t a0_cnd_timedwait(a0_cnd_t* cnd, a0_mtx_t* mtx, const timespec_t* timeout) {
  const uint32_t init_cnd = __atomic_load_n(cnd, __ATOMIC_SEQ_CST);

  a0_mtx_unlock(mtx);

  __tsan_mutex_pre_lock(mtx, 0);
  robust_op_start(mtx);

  errno_t err = EINTR;
  while (err == EINTR) {
    // Wait in the kernel iff the value of it doesn't change (ie somebody else
    // does a wake) from before we unlocked the mutex.
    err = a0_ftx_wait_requeue_pi(cnd, init_cnd, timeout, &mtx->ftx);
  }

  // Timed out waiting.  Signal that back up to the user.
  if (err == ETIMEDOUT) {
    // We have to relock it ourself because the kernel didn't do it.
    errno_t err2 = a0_mtx_timedlock_impl(mtx, NULL);
    robust_op_add(mtx);
    robust_op_end(mtx);
    __tsan_mutex_post_lock(mtx, 0, 0);

    return err2 ? err2 : err;
  }

  // If it failed because somebody else did a wake and changed the value
  // before we actually made it to sleep.
  if (err == EAGAIN) {
    // There's no need to unconditionally set FUTEX_WAITERS here if we're
    // using REQUEUE_PI because the kernel automatically does that in the
    // REQUEUE_PI iff it requeued anybody.
    // If we're not using REQUEUE_PI, then everything is just normal locks
    // etc, so there's no need to do anything special there either.

    // We have to relock it ourself because the kernel didn't do it.
    // fprintf(stderr, "wait on a0_mtx_timedlock_impl\n");
    err = a0_mtx_timedlock_impl(mtx, NULL);
    // fprintf(stderr, "a0_mtx_timedlock_impl done\n");
    robust_op_add(mtx);
    robust_op_end(mtx);
    __tsan_mutex_post_lock(mtx, 0, 0);
    return err;
  }

  // We succeeded in waiting, and the kernel took care of locking the
  // mutex for us and setting FUTEX_WAITERS iff it needed to (for REQUEUE_PI).

  robust_op_add(mtx);

  if (!err) {
    const uint32_t val = __atomic_load_n(&mtx->ftx, __ATOMIC_ACQUIRE);
    robust_op_end(mtx);
    __tsan_mutex_post_lock(mtx, 0, 0);
    return ftx_owner_died(val) ? EOWNERDEAD : A0_OK;
  }

  robust_op_end(mtx);
  __tsan_mutex_post_lock(mtx, 0, 0);
  return err;
}

errno_t a0_cnd_wait(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_timedwait(cnd, mtx, NULL);
}

// The common implementation for broadcast and signal.
// number_requeue is the number of waiters to requeue (probably INT_MAX or 0). 1
// will always be woken.
A0_STATIC_INLINE
errno_t a0_cnd_wake(a0_cnd_t* cnd, a0_mtx_t* mtx, uint32_t cnt) {
  // Make it so that anybody just going to sleep won't.
  // This is where we might accidentally wake more than just 1 waiter with 1
  // signal():
  //   1 already sleeping will be woken but n might never actually make it to
  //     sleep in the kernel because of this.
  uint32_t val = __atomic_add_fetch(cnd, 1, __ATOMIC_SEQ_CST);

  while (true) {
    // This really wants to be FUTEX_REQUEUE_PI, but the kernel doesn't have
    // that... However, the code to support that is in the kernel, so it might
    // be a good idea to patch it to support that and use it iff it's there.
    errno_t err = a0_ftx_cmp_requeue_pi(cnd, val, 1, &mtx->ftx, cnt);
    if (err != EAGAIN) {
      return err;
    }

    // If the value got changed out from under us (aka somebody else did a
    // condition_wake).
    
    // If we're doing a broadcast, the other guy might have done a signal
    // instead, so we have to try again.
    // If we're doing a signal, we have to go again to make sure that 2
    // signals wake 2 processes.
    val = __atomic_load_n(cnd, __ATOMIC_RELAXED);
  }
}

errno_t a0_cnd_signal(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, 1);
}

errno_t a0_cnd_broadcast(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, INT_MAX);
}
