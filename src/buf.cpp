#include <a0/buf.h>
#include <a0/buf.hpp>
#include <a0/err.h>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

Buf::Buf(uint8_t* data, size_t size) {
  set_c(
      &c,
      [&](a0_buf_t* c) {
        *c = {data, size};
        return A0_OK;
      });
}

const uint8_t* Buf::data() const {
  CHECK_C;
  return c->data;
}

uint8_t* Buf::data() {
  return as_mutable(as_const(this)->data());
}

size_t Buf::size() const {
  CHECK_C;
  return c->size;
}

}  // namespace a0
