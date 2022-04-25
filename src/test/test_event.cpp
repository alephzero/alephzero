#include <a0/empty.h>
#include <a0/event.h>
#include <a0/time.h>

#include <doctest.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <thread>

#include "src/err_macro.h"
#include "src/test_util.hpp"

bool is_set(a0_event_t* evt) {
  bool val;
  REQUIRE_OK(a0_event_is_set(evt, &val));
  return val;
}

TEST_CASE("event] default false") {
  a0_event_t evt = A0_EMPTY;
  REQUIRE(!is_set(&evt));
}

TEST_CASE("event] set") {
  a0_event_t evt = A0_EMPTY;
  REQUIRE_OK(a0_event_set(&evt));
  REQUIRE(is_set(&evt));
}

TEST_CASE("event] wait") {
  a0_event_t evt = A0_EMPTY;

  std::thread t([&]() {
    a0_event_set(&evt);
  });

  a0_event_wait(&evt);
  REQUIRE(is_set(&evt));

  t.join();
}

TEST_CASE("event] timedwait success") {
  a0_event_t evt = A0_EMPTY;
  a0_event_set(&evt);

  a0_time_mono_t before = a0::test::timeout_now();
  a0_time_mono_t timeout = a0::test::timeout_in(std::chrono::milliseconds(10));

  REQUIRE_OK(a0_event_timedwait(&evt, &timeout));

  a0_time_mono_t after = a0::test::timeout_now();

  REQUIRE(is_set(&evt));

  uint64_t before_ns = before.ts.tv_sec * 1e9 + before.ts.tv_nsec;
  uint64_t after_ns = after.ts.tv_sec * 1e9 + after.ts.tv_nsec;
  REQUIRE(after_ns > before_ns);
  REQUIRE(after_ns < before_ns + 10 * 1e6);
}

TEST_CASE("event] timedwait timeout") {
  a0_event_t evt = A0_EMPTY;

  a0_time_mono_t before = a0::test::timeout_now();
  a0_time_mono_t timeout = a0::test::timeout_in(std::chrono::milliseconds(10));

  REQUIRE(A0_SYSERR(a0_event_timedwait(&evt, &timeout)) == ETIMEDOUT);

  a0_time_mono_t after = a0::test::timeout_now();

  REQUIRE(!is_set(&evt));

  uint64_t before_ns = before.ts.tv_sec * 1e9 + before.ts.tv_nsec;
  uint64_t after_ns = after.ts.tv_sec * 1e9 + after.ts.tv_nsec;
  REQUIRE(after_ns > before_ns);
  REQUIRE(after_ns > before_ns + 10 * 1e6);
}
