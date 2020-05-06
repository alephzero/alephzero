#ifndef A0_SRC_SYNC_H
#define A0_SRC_SYNC_H

#include <a0/common.h>

#include <limits.h>
#include <linux/futex.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "macros.h"

typedef uint32_t a0_futex_t;

A0_STATIC_INLINE
errno_t a0_futex(a0_futex_t* addr1,
                 int op,
                 int val1,
                 const struct timespec* timeout,
                 a0_futex_t* addr2,
                 int val3) {
  A0_RETURN_ERR_ON_MINUS_ONE(syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3));
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_futex_wait(a0_futex_t* fu, int val, const struct timespec* to) {
  return a0_futex(fu, FUTEX_WAIT, val, to, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_futex_wake(a0_futex_t* fu, int nr) {
  return a0_futex(fu, FUTEX_WAKE, nr, NULL, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_futex_signal(a0_futex_t* fu) {
  return a0_futex_wake(fu, 1);
}

A0_STATIC_INLINE
errno_t a0_futex_broadcast(a0_futex_t* fu) {
  return a0_futex_wake(fu, INT_MAX);
}

#define a0_barrier() asm volatile("" : : : "memory")
#define a0_spin() asm volatile("pause" : : : "memory")

#define a0_atomic_fetch_add(P, V) __sync_fetch_and_add((P), (V))
#define a0_atomic_add_fetch(P, V) __sync_add_and_fetch((P), (V))

#define a0_atomic_fetch_inc(P) a0_atomic_fetch_add((P), 1)
#define a0_atomic_inc_fetch(P) a0_atomic_add_fetch((P), 1)

#define a0_atomic_load(P) __atomic_load_n((P), __ATOMIC_RELAXED)
#define a0_atomic_store(P, V) __atomic_store_n((P), (V), __ATOMIC_RELAXED)

#define a0_cas(P, OV, NV) __sync_val_compare_and_swap((P), (OV), (NV))

#endif  // A0_SRC_SYNC_H
