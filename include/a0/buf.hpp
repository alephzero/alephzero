#pragma once

#include <a0/buf.h>
#include <a0/c_wrap.hpp>

#include <cstddef>
#include <cstdint>

namespace a0 {

struct Buf : details::CppWrap<a0_buf_t> {
  Buf() = default;
  Buf(uint8_t*, size_t);

  const uint8_t* data() const;
  uint8_t* data();
  size_t size() const;
};

}  // namespace a0
