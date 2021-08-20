#include <a0/string_view.hpp>
#include <a0/time.h>
#include <a0/time.hpp>

#include <chrono>
#include <memory>
#include <string>

#include "c_wrap.hpp"

namespace a0 {

TimeMono TimeMono::now() {
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* c) {
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

TimeMono TimeMono::add(std::chrono::nanoseconds dur) {
  CHECK_C;
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* out) {
        return a0_time_mono_add(*c, dur.count(), out);
      });
}

TimeWall TimeWall::now() {
  return make_cpp<TimeWall>(
      [&](a0_time_wall_t* c) {
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
