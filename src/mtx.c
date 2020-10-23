#include "mtx.h"

#include <a0/errno.h>

#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
void robust_op_start(a0_mtx_t* m) {
  assert(!a0_robust_head.list_op_pending);
  a0_robust_head.list_op_pending = (struct robust_list*)m;
  a0_barrier();
}

A0_STATIC_INLINE
void robust_op_end(a0_mtx_t* m) {
  (void)m;
  assert(a0_robust_head.list_op_pending == (robust_list_t*)m);
  a0_barrier();
  a0_robust_head.list_op_pending = NULL;
}

A0_STATIC_INLINE
void robust_op_add(a0_mtx_t* m) {
  assert((a0_mtx_t*)a0_robust_head.list_op_pending == m);
  a0_mtx_t* old_first = (a0_mtx_t*)a0_robust_head.list.next;

  m->prev = (a0_mtx_t*)&a0_robust_head;
  m->next = old_first;

  a0_barrier();

  a0_robust_head.list.next = (robust_list_t*)m;
  if (!robust_is_head(old_first)) {
    old_first->prev = m;
  }
}

A0_STATIC_INLINE
void robust_op_del(a0_mtx_t* m) {
  assert((a0_mtx_t*)a0_robust_head.list_op_pending == m);

  a0_mtx_t* prev = m->prev;
  a0_mtx_t* next = m->next;
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

errno_t a0_mtx_init(a0_mtx_t* m) {
  *m = (a0_mtx_t){0};
  __tsan_mutex_create(m, 0);
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_mtx_trylock_impl(a0_mtx_t* m) {
  int32_t tid = a0_tid();

  int32_t old = m->ftx;
  int32_t own = old & FUTEX_TID_MASK;
  if (own == tid && m->count < 0) {
    old &= FUTEX_OWNER_DIED;
    m->count = 0;
    goto success;
  }
  if (own == FUTEX_TID_MASK) {
    return ENOTRECOVERABLE;
  }
  if (own) {
    return EBUSY;
  }
  if (m->waiters) {
    tid |= FUTEX_WAITERS;
  }
  tid |= old & FUTEX_OWNER_DIED;

  robust_op_start(m);
  if (old != (int32_t)__sync_val_compare_and_swap(&m->ftx, old, tid)) {
    robust_op_end(m);
    if (m->waiters) {
      return ENOTRECOVERABLE;
    }
    return EBUSY;
  }

success:

  if (m->waiters) {
    a0_ftx_unlock_pi(&m->ftx);
    robust_op_end(m);
    return ENOTRECOVERABLE;
  }

  robust_op_add(m);
  robust_op_end(m);

  if (old) {
    m->count = 0;
    return EOWNERDEAD;
  }

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_mtx_lock_impl(a0_mtx_t* m) {
  errno_t ret = a0_mtx_trylock_impl(m);
  if (ret != EBUSY) {
    return ret;
  }

  robust_op_start(m);

  do {
    ret = a0_ftx_lock_pi(&m->ftx, 0, NULL);
  } while (ret == EINTR);

  if (!ret) {
    m->count = -1;
    ret = a0_mtx_trylock_impl(m);
    return ret;
  }

  robust_op_end(m);
  return ret;
}

errno_t a0_mtx_lock(a0_mtx_t* m) {
  __tsan_mutex_pre_lock(m, 0);
  errno_t ret = a0_mtx_lock_impl(m);
  __tsan_mutex_post_lock(m, 0, 0);
  return ret;
}

errno_t a0_mtx_trylock(a0_mtx_t* m) {
  __tsan_mutex_pre_lock(m, __tsan_mutex_try_lock);
  errno_t ret = a0_mtx_trylock_impl(m);
  if (!ret || ret == EOWNERDEAD) {
    __tsan_mutex_post_lock(m, __tsan_mutex_try_lock, 0);
  } else {
    __tsan_mutex_post_lock(m, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  }
  return ret;
}

errno_t a0_mtx_consistent(a0_mtx_t* m) {
  int32_t old = m->ftx;
  int32_t own = old & FUTEX_TID_MASK;
  int32_t tid = a0_tid();

  if (!own || !(old & FUTEX_OWNER_DIED)) {
    return EINVAL;
  }

  if (own != tid) {
    return EPERM;
  }

  __atomic_and_fetch(&m->ftx, ~FUTEX_OWNER_DIED, __ATOMIC_RELAXED);

  return A0_OK;
}

errno_t a0_mtx_unlock(a0_mtx_t* m) {
  int32_t old = m->ftx;
  int32_t new = 0;
  int32_t own = old & FUTEX_TID_MASK;
  int32_t tid = a0_tid();

  if (own != tid) {
    return EPERM;
  }

  __tsan_mutex_pre_unlock(m, 0);
  if (old & FUTEX_OWNER_DIED) {
    new = FUTEX_TID_MASK | FUTEX_OWNER_DIED;
  }

  robust_op_start(m);
  robust_op_del(m);

  if (old < 0 || (int32_t)__sync_val_compare_and_swap(&m->ftx, old, new) != old) {
    if (new) {
      __atomic_store_n(&m->waiters, -1, __ATOMIC_RELAXED);
    }
    a0_ftx_unlock_pi(&m->ftx);
  }

  robust_op_end(m);
  __tsan_mutex_post_unlock(m, 0);

  return A0_OK;
}
