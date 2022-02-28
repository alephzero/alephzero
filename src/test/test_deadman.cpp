#include <a0/deadman.h>
#include <a0/deadman.hpp>
#include <a0/file.h>
#include <a0/time.h>
#include <a0/time.hpp>

#include <doctest.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "src/err_macro.h"
#include "src/test_util.hpp"

struct DeadmanFixture {
  a0_deadman_topic_t topic = {"test"};
  const char* topic_path = "test.deadman";

  DeadmanFixture() {
    a0_file_remove(topic_path);
  }

  ~DeadmanFixture() {
    a0_file_remove(topic_path);
  }

  void REQUIRE_UNLOCKED(a0_deadman_t* d) {
    a0_deadman_state_t state;
    REQUIRE_OK(a0_deadman_state(d, &state));
    REQUIRE(!state.is_taken);
  }

  void REQUIRE_LOCKED(a0_deadman_t* d) {
    a0_deadman_state_t state;
    REQUIRE_OK(a0_deadman_state(d, &state));
    REQUIRE(state.is_taken);
  }

  void REQUIRE_LOCKED_WITH_TKN(a0_deadman_t* d, uint64_t tkn) {
    a0_deadman_state_t state;
    REQUIRE_OK(a0_deadman_state(d, &state));
    REQUIRE(state.is_taken);
    REQUIRE(state.tkn == tkn);
  }
};

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] basic") {
  a0_deadman_t d;
  REQUIRE_OK(a0_deadman_init(&d, topic));

  REQUIRE_UNLOCKED(&d);

  REQUIRE_OK(a0_deadman_take(&d));

  REQUIRE_LOCKED_WITH_TKN(&d, 1);

  REQUIRE_OK(a0_deadman_release(&d));

  REQUIRE_UNLOCKED(&d);

  REQUIRE_OK(a0_deadman_close(&d));
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] close releases") {
  a0_deadman_t d;

  // First deadman instance.
  REQUIRE_OK(a0_deadman_init(&d, topic));

  REQUIRE_UNLOCKED(&d);

  REQUIRE_OK(a0_deadman_take(&d));

  REQUIRE_LOCKED_WITH_TKN(&d, 1);

  REQUIRE_OK(a0_deadman_close(&d));

  // Second deadman instance.
  REQUIRE_OK(a0_deadman_init(&d, topic));

  REQUIRE_UNLOCKED(&d);

  REQUIRE_OK(a0_deadman_take(&d));

  REQUIRE_LOCKED_WITH_TKN(&d, 2);

  REQUIRE_OK(a0_deadman_close(&d));
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] trytake") {
  a0_deadman_t d[2];
  REQUIRE_OK(a0_deadman_init(&d[0], topic));
  REQUIRE_OK(a0_deadman_init(&d[1], topic));

  REQUIRE_OK(a0_deadman_trytake(&d[0]));
  REQUIRE(A0_SYSERR(a0_deadman_trytake(&d[1])) == EBUSY);

  REQUIRE_LOCKED_WITH_TKN(&d[0], 1);
  REQUIRE_LOCKED_WITH_TKN(&d[1], 1);

  REQUIRE_OK(a0_deadman_close(&d[0]));
  REQUIRE_OK(a0_deadman_close(&d[1]));
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] wait_taken wait_released") {
  a0::test::Event evt;

  std::thread t([&]() {
    a0_deadman_t d;
    REQUIRE_OK(a0_deadman_init(&d, topic));
    REQUIRE_OK(a0_deadman_take(&d));
    evt.wait();
    REQUIRE_OK(a0_deadman_close(&d));
  });

  a0_deadman_t d;
  REQUIRE_OK(a0_deadman_init(&d, topic));

  uint64_t tkn;
  REQUIRE_OK(a0_deadman_wait_taken(&d, &tkn));

  REQUIRE_LOCKED_WITH_TKN(&d, tkn);

  evt.set();

  REQUIRE_OK(a0_deadman_wait_released(&d, tkn));

  REQUIRE_UNLOCKED(&d);

  t.join();

  REQUIRE_OK(a0_deadman_close(&d));
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] timed") {
  a0::test::Event evt;

  std::thread t([&]() {
    a0_deadman_t d;
    REQUIRE_OK(a0_deadman_init(&d, topic));
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    auto timeout = a0::test::timeout_in(std::chrono::milliseconds(25));
    REQUIRE_OK(a0_deadman_timedtake(&d, &timeout));
    evt.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    REQUIRE_OK(a0_deadman_close(&d));
  });

  a0_deadman_t d;
  REQUIRE_OK(a0_deadman_init(&d, topic));

  uint64_t tkn;

  auto timeout = a0::test::timeout_in(std::chrono::microseconds(1));
  REQUIRE(A0_SYSERR(a0_deadman_timedwait_taken(&d, &timeout, nullptr)) == ETIMEDOUT);

  timeout = a0::test::timeout_in(std::chrono::milliseconds(50));
  REQUIRE_OK(a0_deadman_timedwait_taken(&d, &timeout, &tkn));

  timeout = a0::test::timeout_in(std::chrono::microseconds(1));
  REQUIRE(A0_SYSERR(a0_deadman_timedtake(&d, &timeout)) == ETIMEDOUT);

  evt.set();

  timeout = a0::test::timeout_in(std::chrono::microseconds(1));
  REQUIRE(A0_SYSERR(a0_deadman_timedwait_released(&d, &timeout, tkn)) == ETIMEDOUT);

  timeout = a0::test::timeout_in(std::chrono::milliseconds(50));
  REQUIRE_OK(a0_deadman_timedwait_released(&d, &timeout, tkn));

  REQUIRE_OK(a0_deadman_close(&d));

  t.join();
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp basic") {
  a0::Deadman d(topic.name);

  REQUIRE(!d.state().is_taken);

  d.take();

  auto state = d.state();
  REQUIRE(state.is_taken);
  REQUIRE(state.tkn == 1);

  d.release();

  REQUIRE(!d.state().is_taken);
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp close releases") {
  {
    a0::Deadman d(topic.name);
    REQUIRE(!d.state().is_taken);
    d.take();
    REQUIRE(d.state().is_taken);
  }

  {
    a0::Deadman d(topic.name);
    REQUIRE(!d.state().is_taken);
    d.take();
    REQUIRE(d.state().is_taken);
  }
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp try_take") {
  a0::Deadman d0(topic.name);
  a0::Deadman d1(topic.name);

  REQUIRE(d0.try_take());
  REQUIRE(!d1.try_take());
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp wait_taken wait_released") {
  a0::test::Event evt;

  std::thread t([&]() {
    a0::Deadman d(topic.name);
    d.take();
    evt.wait();
  });

  a0::Deadman d(topic.name);

  uint64_t tkn = d.wait_taken();
  REQUIRE(d.state().is_taken);

  evt.set();

  d.wait_released(tkn);

  REQUIRE(!d.state().is_taken);

  t.join();
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp timed") {
  a0::test::Event evt;

  std::thread t([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    a0::Deadman d(topic.name);
    d.take(a0::TimeMono::now() + std::chrono::milliseconds(100));
    evt.wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Auto-release.
  });

  a0::Deadman d(topic.name);

  REQUIRE_THROWS_WITH(
      [&]() { d.wait_taken(a0::TimeMono::now() + std::chrono::microseconds(1)); }(),
      strerror(ETIMEDOUT));

  uint64_t tkn = d.wait_taken(a0::TimeMono::now() + std::chrono::milliseconds(200));

  REQUIRE_THROWS_WITH(
      [&]() { d.take(a0::TimeMono::now() + std::chrono::microseconds(1)); }(),
      strerror(ETIMEDOUT));

  evt.set();

  REQUIRE_THROWS_WITH(
      [&]() { d.wait_released(tkn, a0::TimeMono::now() + std::chrono::microseconds(1)); }(),
      strerror(ETIMEDOUT));

  d.wait_released(tkn, a0::TimeMono::now() + std::chrono::milliseconds(200));

  t.join();
}

TEST_CASE_FIXTURE(DeadmanFixture, "deadman] cpp owner died") {
  auto pid = a0::test::subproc([&]() {
    a0::Deadman d(topic.name);
    d.take();
    pause();
  });

  a0::Deadman d(topic.name);
  d.wait_taken();
  REQUIRE(d.state().is_taken);

  kill(pid, SIGKILL);

  int ret_code;
  waitpid(pid, &ret_code, 0);

  d.take();
}
