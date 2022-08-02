#include <a0/string_view.hpp>
#include <a0/time.h>
#include <a0/time.hpp>

#include <chrono>
#include <memory>
#include <string>

#include "c_wrap.hpp"

namespace a0 {

TimeMono TIMEOUT_IMMEDIATE = cpp_wrap<TimeMono>(A0_TIMEOUT_IMMEDIATE);

TimeMono TIMEOUT_NEVER = cpp_wrap<TimeMono>(A0_TIMEOUT_NEVER);

TimeMono TimeMono::now() {
  return make_cpp<TimeMono>(
      [](a0_time_mono_t* c) {
        return a0_time_mono_now(c);
      });
}

std::string TimeMono::to_string() {
  CHECK_C;
  char mono_str[20];
  check(a0_time_mono_str(*c, mono_str));
  return mono_str;
}

TimeMono TimeMono::parse(string_view str) {
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* c) {
        return a0_time_mono_parse(str.data(), c);
      });
}

TimeMono& TimeMono::operator+=(std::chrono::nanoseconds dur) {
  c = (*this + dur).c;
  return *this;
}

TimeMono& TimeMono::operator-=(std::chrono::nanoseconds dur) {
  return *this += (-dur);
}

TimeMono TimeMono::operator+(std::chrono::nanoseconds dur) const {
  CHECK_C;
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* out) {
        return a0_time_mono_add(*c, dur.count(), out);
      });
}

TimeMono TimeMono::operator-(std::chrono::nanoseconds dur) const {
  return *this + (-dur);
}

bool TimeMono::operator<(TimeMono rhs) const {
  CHECK_C;
  check(__PRETTY_FUNCTION__, &rhs);
  bool ret;
  a0_time_mono_less(*c, *rhs.c, &ret);
  return ret;
}

bool TimeMono::operator==(TimeMono rhs) const {
  CHECK_C;
  check(__PRETTY_FUNCTION__, &rhs);
  bool ret;
  a0_time_mono_equal(*c, *rhs.c, &ret);
  return ret;
}

bool TimeMono::operator!=(TimeMono rhs) const {
  return !(*this == rhs);
}

bool TimeMono::operator>(TimeMono rhs) const {
  return rhs < *this;
}

bool TimeMono::operator>=(TimeMono rhs) const {
  return !(*this < rhs);
}

bool TimeMono::operator<=(TimeMono rhs) const {
  return !(*this > rhs);
}

TimeWall TimeWall::now() {
  return make_cpp<TimeWall>(
      [](a0_time_wall_t* c) {
        return a0_time_wall_now(c);
      });
}

std::string TimeWall::to_string() {
  CHECK_C;
  char wall_str[36];
  check(a0_time_wall_str(*c, wall_str));
  return wall_str;
}

TimeWall TimeWall::parse(string_view str) {
  return make_cpp<TimeWall>(
      [&](a0_time_wall_t* c) {
        return a0_time_wall_parse(str.data(), c);
      });
}

}  // namespace a0
