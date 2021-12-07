#include <a0/arena.hpp>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/time.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>

#include "c_wrap.hpp"

namespace a0 {

bool TransportReaderLocked::empty() const {
  CHECK_C;
  bool ret;
  check(a0_transport_reader_empty(&*c, &ret));
  return ret;
}

uint64_t TransportReaderLocked::seq_low() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_reader_seq_low(&*c, &ret));
  return ret;
}

uint64_t TransportReaderLocked::seq_high() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_reader_seq_high(&*c, &ret));
  return ret;
}

bool TransportReaderLocked::iter_valid() const {
  CHECK_C;
  bool ret;
  check(a0_transport_reader_iter_valid(&*c, &ret));
  return ret;
}

Frame TransportReaderLocked::frame() const {
  CHECK_C;
  Frame ret;
  check(a0_transport_reader_frame(&*c, &ret));
  return ret;
}

void TransportReaderLocked::jump(size_t off) {
  CHECK_C;
  check(a0_transport_reader_jump(&*c, off));
}

void TransportReaderLocked::jump_head() {
  CHECK_C;
  check(a0_transport_reader_jump_head(&*c));
}

void TransportReaderLocked::jump_tail() {
  CHECK_C;
  check(a0_transport_reader_jump_tail(&*c));
}

bool TransportReaderLocked::has_next() const {
  CHECK_C;
  bool ret;
  check(a0_transport_reader_has_next(&*c, &ret));
  return ret;
}

void TransportReaderLocked::step_next() {
  CHECK_C;
  check(a0_transport_reader_step_next(&*c));
}

bool TransportReaderLocked::has_prev() const {
  CHECK_C;
  bool ret;
  check(a0_transport_reader_has_prev(&*c, &ret));
  return ret;
}

void TransportReaderLocked::step_prev() {
  CHECK_C;
  check(a0_transport_reader_step_prev(&*c));
}

namespace {

a0_predicate_t pred(std::function<bool()>* fn) {
  return {
      .user_data = fn,
      .fn = [](void* user_data, bool* out) {
        try {
          *out = (*(std::function<bool()>*)user_data)();
        } catch (const std::exception& e) {
          size_t len = std::min(1023ul, strnlen(e.what(), 1023));
          memcpy(a0_err_msg, e.what(), len);
          a0_err_msg[1023] = '\0';
          return A0_ERR_CUSTOM_MSG;
        }
        return A0_OK;
      },
  };
}

}  // namespace

void TransportReaderLocked::wait(std::function<bool()> fn) {
  CHECK_C;
  check(a0_transport_reader_wait(&*c, pred(&fn)));
}

void TransportReaderLocked::wait_for(std::function<bool()> fn, std::chrono::nanoseconds dur) {
  wait_until(fn, TimeMono::now() + dur);
}

void TransportReaderLocked::wait_until(std::function<bool()> fn, TimeMono tm) {
  CHECK_C;
  check(a0_transport_reader_timedwait(&*c, pred(&fn), *tm.c));
}

TransportReader::TransportReader(Arena arena) {
  set_c(
      &c,
      [&](a0_transport_reader_t* c) {
        return a0_transport_reader_init(c, *arena.c);
      },
      [arena](a0_transport_reader_t*) {});
}

TransportReaderLocked TransportReader::lock() {
  CHECK_C;
  auto save = c;
  return make_cpp<TransportReaderLocked>(
      [&](a0_transport_reader_locked_t* trl) {
        return a0_transport_reader_lock(&*c, trl);
      },
      [save](a0_transport_reader_locked_t* trl) {
        a0_transport_reader_unlock(trl);
      });
}

bool TransportWriterLocked::empty() const {
  CHECK_C;
  bool ret;
  check(a0_transport_writer_empty(&*c, &ret));
  return ret;
}

uint64_t TransportWriterLocked::seq_low() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_writer_seq_low(&*c, &ret));
  return ret;
}

uint64_t TransportWriterLocked::seq_high() const {
  CHECK_C;
  uint64_t ret;
  check(a0_transport_writer_seq_high(&*c, &ret));
  return ret;
}

size_t TransportWriterLocked::used_space() const {
  CHECK_C;
  size_t ret;
  check(a0_transport_writer_used_space(&*c, &ret));
  return ret;
}

void TransportWriterLocked::resize(size_t size) {
  CHECK_C;
  check(a0_transport_writer_resize(&*c, size));
}

Frame TransportWriterLocked::alloc(size_t size) {
  CHECK_C;
  Frame ret;
  check(a0_transport_writer_alloc(&*c, size, &ret));
  return ret;
}

bool TransportWriterLocked::alloc_evicts(size_t size) const {
  CHECK_C;
  bool ret;
  check(a0_transport_writer_alloc_evicts(&*c, size, &ret));
  return ret;
}

void TransportWriterLocked::commit() {
  CHECK_C;
  check(a0_transport_writer_commit(&*c));
}

TransportReaderLocked TransportWriterLocked::as_reader() {
  CHECK_C;
  auto save = c;
  auto tr = std::make_shared<a0_transport_reader_t>();
  return make_cpp<TransportReaderLocked>(
      [&](a0_transport_reader_locked_t* trl) {
        return a0_transport_writer_as_reader(&*c, &*tr, trl);
      },
      [save, tr](a0_transport_reader_locked_t*) {
      });
}

TransportWriter::TransportWriter(Arena arena) {
  set_c(
      &c,
      [&](a0_transport_writer_t* c) {
        return a0_transport_writer_init(c, *arena.c);
      },
      [arena](a0_transport_writer_t*) {});
}

TransportWriterLocked TransportWriter::lock() {
  CHECK_C;
  auto save = c;
  return make_cpp<TransportWriterLocked>(
      [&](a0_transport_writer_locked_t* twl) {
        return a0_transport_writer_lock(&*c, twl);
      },
      [save](a0_transport_writer_locked_t* twl) {
        a0_transport_writer_unlock(twl);
      });
}

}  // namespace a0
