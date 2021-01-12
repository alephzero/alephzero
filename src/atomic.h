#ifndef A0_SRC_ATOMIC_H
#define A0_SRC_ATOMIC_H

#include "macros.h"

A0_STATIC_INLINE
void a0_barrier() {
  asm volatile(""
               :
               :
               : "memory");
}

A0_STATIC_INLINE
void a0_spin() {
  asm volatile("pause"
               :
               :
               : "memory");
}

#define a0_atomic_fetch_add(P, V) __atomic_fetch_add((P), (V), __ATOMIC_RELAXED)
#define a0_atomic_add_fetch(P, V) __atomic_add_fetch((P), (V), __ATOMIC_RELAXED)

#define a0_atomic_fetch_and(P, V) __atomic_fetch_and((P), (V), __ATOMIC_RELAXED)
#define a0_atomic_and_fetch(P, V) __atomic_and_fetch((P), (V), __ATOMIC_RELAXED)

#define a0_atomic_fetch_or(P, V) __atomic_fetch_or((P), (V), __ATOMIC_RELAXED)
#define a0_atomic_or_fetch(P, V) __atomic_or_fetch((P), (V), __ATOMIC_RELAXED)

#define a0_atomic_load(P) __atomic_load_n((P), __ATOMIC_RELAXED)
#define a0_atomic_store(P, V) __atomic_store_n((P), (V), __ATOMIC_RELAXED)

// TODO(lshamis): Switch from __sync to __atomic.
#define a0_cas_val(P, OV, NV) __sync_val_compare_and_swap((P), (OV), (NV))
#define a0_cas(P, OV, NV) __sync_bool_compare_and_swap((P), (OV), (NV))

#endif  // A0_SRC_ATOMIC_H
