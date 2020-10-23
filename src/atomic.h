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

#define a0_atomic_fetch_add(P, V) __sync_fetch_and_add((P), (V))
#define a0_atomic_add_fetch(P, V) __sync_add_and_fetch((P), (V))

#define a0_atomic_fetch_inc(P) a0_atomic_fetch_add((P), 1)
#define a0_atomic_inc_fetch(P) a0_atomic_add_fetch((P), 1)

#define a0_atomic_load(P) __atomic_load_n((P), __ATOMIC_RELAXED)
#define a0_atomic_store(P, V) __atomic_store_n((P), (V), __ATOMIC_RELAXED)

#define a0_cas(P, OV, NV) __sync_val_compare_and_swap((P), (OV), (NV))

#endif  // A0_SRC_ATOMIC_H
