#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/buf.hpp>
#include <a0/err.h>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

Arena::Arena(Buf buf, a0_arena_mode_t mode) {
  set_c(
      &c,
      [&](a0_arena_t* c) {
        *c = {*buf.c, mode};
        return A0_OK;
      },
      [buf](a0_arena_t*) {});
}

const Buf Arena::buf() const {
  CHECK_C;
  auto save = c;
  return make_cpp<Buf>(
      [&](a0_buf_t* buf) {
        *buf = c->buf;
        return A0_OK;
      },
      [save](a0_buf_t*) {});
}

Buf Arena::buf() {
  return as_mutable(as_const(this)->buf());
}

a0_arena_mode_t Arena::mode() const {
  CHECK_C;
  return c->mode;
}

Arena::operator const Buf() const {
  return buf();
}

Arena::operator Buf() {
  return buf();
}

}  // namespace a0
