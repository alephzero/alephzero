#include <a0/buf.hpp>

#include "c_wrap.hpp"

namespace a0 {

Buf::Buf(uint8_t* ptr, size_t size) {
  set_c(
      &c,
      [&](a0_buf_t* c) {
        *c = {ptr, size};
        return A0_OK;
      },
      nullptr);
}

const uint8_t* Buf::ptr() const {
  CHECK_C;
  return c->ptr;
}

uint8_t* Buf::ptr() {
  return as_mutable(as_const(this)->ptr());
}

size_t Buf::size() const {
  CHECK_C;
  return c->size;
}

}  // namespace a0
