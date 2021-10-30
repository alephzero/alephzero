#include <a0/time.h>
#include <a0/time.hpp>

#include <doctest.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include "src/test_util.hpp"

TEST_CASE("time] mono") {
  a0_time_mono_t now;
  REQUIRE_OK(a0_time_mono_now(&now));

  a0_time_mono_t fut;
  REQUIRE_OK(a0_time_mono_add(now, 1, &fut));

  char mono_str[20];
  REQUIRE_OK(a0_time_mono_str(fut, mono_str));

  a0_time_mono_t recovered;
  REQUIRE_OK(a0_time_mono_parse(mono_str, &recovered));

  struct timespec want = now.ts;
  want.tv_nsec++;
  if (want.tv_nsec >= 1e9) {
    want.tv_sec++;
    want.tv_nsec -= 1e9;
  }

  REQUIRE(want.tv_sec == recovered.ts.tv_sec);
  REQUIRE(want.tv_nsec == recovered.ts.tv_nsec);
}

TEST_CASE("time] cpp mono") {
  a0::TimeMono now = a0::TimeMono::now();
  a0::TimeMono fut = now + std::chrono::nanoseconds(1);
  std::string serial = fut.to_string();
  REQUIRE(serial.size() == 19);
  a0::TimeMono recovered = a0::TimeMono::parse(serial);

  struct timespec want = now.c->ts;
  want.tv_nsec++;
  if (want.tv_nsec >= 1e9) {
    want.tv_sec++;
    want.tv_nsec -= 1e9;
  }

  REQUIRE(want.tv_sec == recovered.c->ts.tv_sec);
  REQUIRE(want.tv_nsec == recovered.c->ts.tv_nsec);
}

TEST_CASE("time] cpp mono operators") {
  a0::TimeMono now = a0::TimeMono::now();
  a0::TimeMono fut = now + std::chrono::nanoseconds(1);
  a0::TimeMono now_again = fut - std::chrono::nanoseconds(1);
  a0::TimeMono fut_again = now_again;
  fut_again += std::chrono::nanoseconds(1);
  a0::TimeMono now_again_again = fut_again;
  now_again_again -= std::chrono::nanoseconds(1);

  REQUIRE(now.c.get() != now_again.c.get());
  REQUIRE(fut.c.get() != fut_again.c.get());

  REQUIRE(now == now);
  REQUIRE(now == now_again);
  REQUIRE(now == now_again_again);
  REQUIRE(fut == fut_again);

  REQUIRE(now != fut);
  REQUIRE(now <= now);
  REQUIRE(now <= fut);
  REQUIRE(now < fut);
  REQUIRE(fut > now);
  REQUIRE(fut >= now);
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

TEST_CASE("time] cpp wall") {
  a0::TimeWall time_wall = a0::TimeWall::now();
  std::string serial = time_wall.to_string();
  REQUIRE(serial.size() == 35);
  a0::TimeWall recovered = a0::TimeWall::parse(serial);

  REQUIRE(time_wall.c->ts.tv_sec == recovered.c->ts.tv_sec);
  REQUIRE(time_wall.c->ts.tv_nsec == recovered.c->ts.tv_nsec);
}
