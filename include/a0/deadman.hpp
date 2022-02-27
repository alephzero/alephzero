#pragma once

#include <a0/c_wrap.hpp>
#include <a0/deadman.h>
#include <a0/time.hpp>

namespace a0 {

struct DeadmanTopic {
  std::string name;

  DeadmanTopic() = default;

  DeadmanTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : DeadmanTopic(std::string(name)) {}

  DeadmanTopic(std::string name)  // NOLINT(google-explicit-constructor)
      : name{std::move(name)} {}
};

struct Deadman : details::CppWrap<a0_deadman_t> {
  struct State {
    bool is_taken;
    bool is_owner;
    uint64_t tkn;
  };

  Deadman() = default;
  explicit Deadman(DeadmanTopic);

  void take();
  bool try_take();
  void take(TimeMono);
  void release();

  uint64_t wait_taken();
  uint64_t wait_taken(TimeMono);
  void wait_released(uint64_t);
  void wait_released(uint64_t, TimeMono);

  State state();
};

}  // namespace a0
