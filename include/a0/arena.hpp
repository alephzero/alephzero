#pragma once

#include <a0/arena.h>
#include <a0/buf.hpp>
#include <a0/c_wrap.hpp>

namespace a0 {

struct Arena : details::CppWrap<a0_arena_t> {
  Arena() = default;
  Arena(Buf, a0_arena_mode_t);

  Buf buf() const;
  a0_arena_mode_t mode() const;

  /// Implicit conversions.
  operator Buf() const;  // NOLINT(google-explicit-constructor)
};

}  // namespace a0
