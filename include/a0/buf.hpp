#pragma once

#include <a0/buf.h>
#include <a0/c_wrap.hpp>

#include <cstddef>
#include <cstdint>

namespace a0 {

struct Buf : details::CppWrap<a0_buf_t> {
  Buf() = default;
  /// Construct a buffer wrapping the given memory.
  Buf(uint8_t*, size_t);

  /// Constant reference to the underlying memory.
  const uint8_t* data() const;
  /// Mutable reference to the underlying memory.
  uint8_t* data();
  /// Size of the underlying memory.
  size_t size() const;
};

}  // namespace a0
