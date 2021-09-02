#include <a0/arena.hpp>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/time.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <errno.h>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

bool TransportLocked::empty() const {
  CHECK_C;
  bool ret;
  check(a0_transport_empty(*c, &ret));
  return ret;
}

uint64_t TransportLocked::seq_low() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_seq_low(*c, &ret));
  return ret;
}

uint64_t TransportLocked::seq_high() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_seq_high(*c, &ret));
  return ret;
}

size_t TransportLocked::used_space() const {
  CHECK_C;
  size_t ret;
  check(a0_transport_used_space(*c, &ret));
  return ret;
}

void TransportLocked::resize(size_t size) {
  CHECK_C;
  check(a0_transport_resize(*c, size));
}

bool TransportLocked::ptr_valid() const {
  CHECK_C;
  bool ret;
  check(a0_transport_ptr_valid(*c, &ret));
  return ret;
}

const Frame TransportLocked::frame() const {
  CHECK_C;
  Frame ret;
  check(a0_transport_frame(*c, &ret));
  return ret;
}

Frame TransportLocked::frame() {
  return as_mutable(as_const(this)->frame());
}

void TransportLocked::jump_head() {
  CHECK_C;
  check(a0_transport_jump_head(*c));
}

void TransportLocked::jump_tail() {
  CHECK_C;
  check(a0_transport_jump_tail(*c));
}

bool TransportLocked::has_next() const {
  CHECK_C;
  bool ret;
  check(a0_transport_has_next(*c, &ret));
  return ret;
}

void TransportLocked::step_next() {
  CHECK_C;
  check(a0_transport_step_next(*c));
}

bool TransportLocked::has_prev() const {
  CHECK_C;
  bool ret;
  check(a0_transport_has_prev(*c, &ret));
  return ret;
}

void TransportLocked::step_prev() {
  CHECK_C;
  check(a0_transport_step_prev(*c));
}

Frame TransportLocked::alloc(size_t size) {
  CHECK_C;
  Frame ret;
  check(a0_transport_alloc(*c, size, &ret));
  return ret;
}

bool TransportLocked::alloc_evicts(size_t size) const {
  CHECK_C;
  bool ret;
  check(a0_transport_alloc_evicts(*c, size, &ret));
  return ret;
}

void TransportLocked::commit() {
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
        } catch (const std::exception& e) {
          size_t len = std::min(1024ul, strnlen(e.what(), 1024));
          memcpy(a0_err_msg, e.what(), len);
          return A0_ERRCODE_CUSTOM_MSG;
        }
        return A0_OK;
      },
  };
}

}  // namespace

void TransportLocked::wait(std::function<bool()> fn) {
  CHECK_C;
  check(a0_transport_wait(*c, pred(&fn)));
}

void TransportLocked::wait_for(std::function<bool()> fn, std::chrono::nanoseconds dur) {
  wait_until(fn, TimeMono::now().add(dur));
}

void TransportLocked::wait_until(std::function<bool()> fn, TimeMono tm) {
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

TransportLocked Transport::lock() {
  CHECK_C;
  auto save = c;
  return make_cpp<TransportLocked>(
      [&](a0_transport_locked_t* lk) {
        return a0_transport_lock(&*c, lk);
      },
      [save](a0_transport_locked_t* lk) {
        a0_transport_unlock(*lk);
      });
}

}  // namespace a0
