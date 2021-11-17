#pragma once

#include <a0/arena.hpp>
#include <a0/c_wrap.hpp>
#include <a0/time.hpp>
#include <a0/transport.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace a0 {

using Frame = a0_transport_frame_t;

struct TransportLocked : details::CppWrap<a0_transport_locked_t> {
  bool empty() const;
  uint64_t seq_low() const;
  uint64_t seq_high() const;

  size_t used_space() const;
  void resize(size_t);

  bool iter_valid() const;
  Frame frame() const;

  void jump(size_t off);
  void jump_head();
  void jump_tail();
  bool has_next() const;
  void step_next();
  bool has_prev() const;
  void step_prev();

  Frame alloc(size_t);
  bool alloc_evicts(size_t) const;

  void commit();

  void wait(std::function<bool()>);
  void wait_for(std::function<bool()>, std::chrono::nanoseconds);
  void wait_until(std::function<bool()>, TimeMono);
};

struct Transport : details::CppWrap<a0_transport_t> {
  Transport() = default;
  explicit Transport(Arena);

  TransportLocked lock();
};

}  // namespace a0
