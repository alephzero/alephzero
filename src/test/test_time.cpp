#include <a0/time.h>

#include <doctest.h>

#include <cstdint>
#include <ctime>

#include "src/test_util.hpp"

TEST_CASE("time] mono") {
  uint64_t mono_ts;
  REQUIRE_OK(a0_time_mono_now(&mono_ts));

  char mono_str[20];
  REQUIRE_OK(a0_time_mono_str(mono_ts, mono_str));

  uint64_t recovered_ts;
  REQUIRE_OK(a0_time_mono_parse(mono_str, &recovered_ts));

  REQUIRE(mono_ts == recovered_ts);
}

TEST_CASE("time] wall") {
  timespec wall_ts;
  REQUIRE_OK(a0_time_wall_now(&wall_ts));

  char wall_str[36];
  REQUIRE_OK(a0_time_wall_str(wall_ts, wall_str));

  timespec recovered_ts;
  REQUIRE_OK(a0_time_wall_parse(wall_str, &recovered_ts));

  REQUIRE(wall_ts.tv_nsec == recovered_ts.tv_nsec);
  REQUIRE(wall_ts.tv_sec == recovered_ts.tv_sec);
}
