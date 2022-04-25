#pragma once

#include <a0/c_wrap.hpp>
#include <a0/string_view.hpp>
#include <a0/time.h>

#include <chrono>
#include <cstdint>
#include <functional>

namespace a0 {

struct TimeMono : details::CppWrap<a0_time_mono_t> {
  static TimeMono now();
  std::string to_string();
  static TimeMono parse(string_view);

  TimeMono& operator+=(std::chrono::nanoseconds);
  TimeMono& operator-=(std::chrono::nanoseconds);
  TimeMono operator+(std::chrono::nanoseconds) const;
  TimeMono operator-(std::chrono::nanoseconds) const;
  bool operator<(TimeMono) const;
  bool operator==(TimeMono) const;
  bool operator!=(TimeMono) const;
  bool operator>(TimeMono) const;
  bool operator>=(TimeMono) const;
  bool operator<=(TimeMono) const;
};

extern TimeMono TIMEOUT_IMMEDIATE;
extern TimeMono TIMEOUT_NEVER;

struct TimeWall : details::CppWrap<a0_time_wall_t> {
  static TimeWall now();
  std::string to_string();
  static TimeWall parse(string_view);
};

}  // namespace a0
