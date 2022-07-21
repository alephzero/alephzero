#ifndef A0_ALIGN_H
#define A0_ALIGN_H

#include <a0/inline.h>

#include <stdalign.h>
#include <stddef.h>

A0_STATIC_INLINE
size_t a0_align(size_t pt) {
  return ((pt + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

#endif  // A0_ALIGN_H
