#include <a0/event.h>
#include <a0/time.h>

#include <doctest.h>

#include <cstdint>
#include <ctime>
#include <thread>

TEST_CASE("event] default false") {
  a0_event_t evt;
  a0_event_init(&evt);

  REQUIRE(!a0_event_is_set(&evt));

  a0_event_close(&evt);
}

TEST_CASE("event] set") {
  a0_event_t evt;
  a0_event_init(&evt);

  a0_event_set(&evt);
  REQUIRE(a0_event_is_set(&evt));

  a0_event_close(&evt);
}

TEST_CASE("event] wait") {
  a0_event_t evt;
  a0_event_init(&evt);

  std::thread t([&]() {
    a0_event_set(&evt);
  });
  a0_event_wait(&evt);
  REQUIRE(a0_event_is_set(&evt));
  t.join();

  a0_event_close(&evt);
}

TEST_CASE("event] timedwait success") {
  a0_event_t evt;
  a0_event_init(&evt);

  std::thread t([&]() {
    a0_event_set(&evt);
  });

  a0_time_mono_t before;
  a0_time_mono_now(&before);

  a0_time_mono_t timeout;
  a0_time_mono_add(before, 10 * 1e6, &timeout);

  a0_event_timedwait(&evt, timeout);

  a0_time_mono_t after;
  a0_time_mono_now(&after);

  REQUIRE(a0_event_is_set(&evt));

  uint64_t before_ns = before.ts.tv_sec * 1e9 + before.ts.tv_nsec;
  uint64_t after_ns = after.ts.tv_sec * 1e9 + after.ts.tv_nsec;
  REQUIRE(after_ns > before_ns);
  REQUIRE(after_ns < before_ns + 10 * 1e6);

  t.join();

  a0_event_close(&evt);
}

TEST_CASE("event] timedwait timeout") {
  a0_event_t evt;
  a0_event_init(&evt);

  a0_time_mono_t before;
  a0_time_mono_now(&before);

  a0_time_mono_t timeout;
  a0_time_mono_add(before, 10 * 1e6, &timeout);

  a0_event_timedwait(&evt, timeout);

  a0_time_mono_t after;
  a0_time_mono_now(&after);

  REQUIRE(!a0_event_is_set(&evt));

  uint64_t before_ns = before.ts.tv_sec * 1e9 + before.ts.tv_nsec;
  uint64_t after_ns = after.ts.tv_sec * 1e9 + after.ts.tv_nsec;
  REQUIRE(after_ns > before_ns);
  REQUIRE(after_ns > before_ns + 10 * 1e6);

  a0_event_close(&evt);
}
