#include <a0/transport.hpp>
#include <a0/arena.hpp>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/time.hpp>
#include <a0/transport.h>

#include <errno.h>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

bool LockedTransport::empty() const {
  CHECK_C;
  bool ret;
  check(a0_transport_empty(*c, &ret));
  return ret;
}

uint64_t LockedTransport::seq_low() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_seq_low(*c, &ret));
  return ret;
}

uint64_t LockedTransport::seq_high() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_seq_high(*c, &ret));
  return ret;
}

size_t LockedTransport::used_space() const {
  CHECK_C;
  size_t ret;
  check(a0_transport_used_space(*c, &ret));
  return ret;
}

void LockedTransport::resize(size_t size) {
  CHECK_C;
  check(a0_transport_resize(*c, size));
}

bool LockedTransport::ptr_valid() const {
  CHECK_C;
  bool ret;
  check(a0_transport_ptr_valid(*c, &ret));
  return ret;
}

const Frame LockedTransport::frame() const {
  CHECK_C;
  Frame ret;
  check(a0_transport_frame(*c, &ret));
  return ret;
}

Frame LockedTransport::frame() {
  return as_mutable(as_const(this)->frame());
}

void LockedTransport::jump_head() {
  CHECK_C;
  check(a0_transport_jump_head(*c));
}

void LockedTransport::jump_tail() {
  CHECK_C;
  check(a0_transport_jump_tail(*c));
}

bool LockedTransport::has_next() const {
  CHECK_C;
  bool ret;
  check(a0_transport_has_next(*c, &ret));
  return ret;
}

void LockedTransport::step_next() {
  CHECK_C;
  check(a0_transport_step_next(*c));
}

bool LockedTransport::has_prev() const {
  CHECK_C;
  bool ret;
  check(a0_transport_has_prev(*c, &ret));
  return ret;
}

void LockedTransport::step_prev() {
  CHECK_C;
  check(a0_transport_step_prev(*c));
}

Frame LockedTransport::alloc(size_t size) {
  CHECK_C;
  Frame ret;
  check(a0_transport_alloc(*c, size, &ret));
  return ret;
}

bool LockedTransport::alloc_evicts(size_t size) const {
  CHECK_C;
  bool ret;
  check(a0_transport_alloc_evicts(*c, size, &ret));
  return ret;
}

void LockedTransport::commit() {
  CHECK_C;
  check(a0_transport_commit(*c));
}

namespace {

a0_predicate_t pred(std::function<bool()>* fn) {
  return {
      .user_data = fn,
      .fn = [](void* user_data, bool* out) {
        try {
          *out = (*(std::function<bool()>*)user_data)();
        } catch (...) {
          return EINVAL;
        }
        return A0_OK;
      },
  };
}

}  // namespace

void LockedTransport::wait(std::function<bool()> fn) {
  CHECK_C;
  check(a0_transport_wait(*c, pred(&fn)));
}

void LockedTransport::wait_for(std::function<bool()> fn, std::chrono::nanoseconds dur) {
  wait_until(fn, TimeMono::now().add(dur));
}

void LockedTransport::wait_until(std::function<bool()> fn, TimeMono tm) {
  CHECK_C;
  check(a0_transport_timedwait(*c, pred(&fn), *tm.c));
}

Transport::Transport(Arena arena) {
  set_c(
      &c,
      [&](a0_transport_t* c) {
        return a0_transport_init(c, *arena.c);
      },
      [arena](a0_transport_t*) {});
}

LockedTransport Transport::lock() {
  CHECK_C;
  auto save = c;
  return make_cpp<LockedTransport>(
      [&](a0_locked_transport_t* lk) {
        return a0_transport_lock(&*c, lk);
      },
      [save](a0_locked_transport_t* lk) {
        a0_transport_unlock(*lk);
      });
}

}  // namespace a0