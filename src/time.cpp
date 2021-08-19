#include <a0/time.h>
#include <a0/time.hpp>

#include "c_wrap.hpp"

namespace a0 {


// struct TimeMono : details::CppWrap<a0_time_mono_t> {
//   static TimeMono now();
//   std::string to_string();
//   static TimeMono parse(const std::string&);
//   TimeMono add(std::chrono::nanoseconds);
// };

// struct TimeWall : details::CppWrap<a0_time_wall_t> {
//   static TimeWall now();
//   std::string to_string();
//   static TimeWall parse(const std::string&);
// };

TimeMono TimeMono::now() {
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* c) {
        return a0_time_mono_now(c);
      },
      nullptr);
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
      },
      nullptr);
}

TimeMono TimeMono::add(std::chrono::nanoseconds dur) {
  CHECK_C;
  return make_cpp<TimeMono>(
      [&](a0_time_mono_t* out) {
        return a0_time_mono_add(*c, dur.count(), out);
      },
      nullptr);
}

TimeWall TimeWall::now() {
  return make_cpp<TimeWall>(
      [&](a0_time_wall_t* c) {
        return a0_time_wall_now(c);
      },
      nullptr);
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
      },
      nullptr);
}

}  // namespace a0
