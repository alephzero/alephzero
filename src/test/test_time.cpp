#include <a0/time.h>

#include <doctest.h>

#include <ctime>

#include "src/test_util.hpp"

TEST_CASE("time] mono") {
  a0_time_mono_t time_mono;
  REQUIRE_OK(a0_time_mono_now(&time_mono));

  char mono_str[20];
  REQUIRE_OK(a0_time_mono_str(time_mono, mono_str));

  a0_time_mono_t recovered;
  REQUIRE_OK(a0_time_mono_parse(mono_str, &recovered));

  REQUIRE(time_mono.ts.tv_sec == recovered.ts.tv_sec);
  REQUIRE(time_mono.ts.tv_nsec == recovered.ts.tv_nsec);
}

TEST_CASE("time] wall") {
  a0_time_wall_t time_wall;
  REQUIRE_OK(a0_time_wall_now(&time_wall));

  char wall_str[36];
  REQUIRE_OK(a0_time_wall_str(time_wall, wall_str));

  a0_time_wall_t recovered;
  REQUIRE_OK(a0_time_wall_parse(wall_str, &recovered));

  REQUIRE(time_wall.ts.tv_sec == recovered.ts.tv_sec);
  REQUIRE(time_wall.ts.tv_nsec == recovered.ts.tv_nsec);
}
