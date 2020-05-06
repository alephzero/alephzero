#pragma once

#include <a0/alloc.h>

#include <functional>

#include "macros.h"
#include "scope.hpp"

namespace a0 {

A0_STATIC_INLINE
a0::scope<a0_alloc_t> scope_realloc() {
  a0_alloc_t alloc;
  a0_realloc_allocator_init(&alloc);
  return a0::scope<a0_alloc_t>(alloc, [](a0_alloc_t* alloc_) {
    a0_realloc_allocator_close(alloc_);
  });
}

}  // namespace a0
