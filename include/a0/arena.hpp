#pragma once

#include <a0/arena.h>
#include <a0/buf.hpp>
#include <a0/c_wrap.hpp>

namespace a0 {

struct Arena : details::CppWrap<a0_arena_t> {
  Arena() = default;
  Arena(Buf, a0_arena_mode_t);

  const Buf buf() const;
  Buf buf();
  a0_arena_mode_t mode() const;

  /// Implicit conversions.
  operator const Buf() const;
  operator Buf();
};

}  // namespace a0
