#pragma once

#include <a0/arena.h>
#include <a0/buf.hpp>
#include <a0/c_wrap.hpp>

namespace a0 {

struct Arena : details::CppWrap<a0_arena_t> {
  Arena() = default;
  /// Construct an arena with the given buffer and mode.
  Arena(Buf, a0_arena_mode_t);

  /// Underlying buffer.
  Buf buf() const;
  /// Access mode.
  a0_arena_mode_t mode() const;

  /// Implicit conversions.
  operator Buf() const;  // NOLINT(google-explicit-constructor)
};

}  // namespace a0
