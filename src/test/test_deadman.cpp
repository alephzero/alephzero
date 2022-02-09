#include <a0/deadman.h>
#include <a0/empty.h>

#include <doctest.h>

#include <atomic>
#include <thread>

#include "src/err_macro.h"
#include "src/test_util.hpp"

void REQUIRE_OK_OR_SYSERR(a0_err_t err, int syserr) {
  REQUIRE((!err || A0_SYSERR(err) == syserr));
}

void REQUIRE_OK_OR_SYSERR(a0_err_t err, int syserr_1, int syserr_2) {
  REQUIRE((!err || A0_SYSERR(err) == syserr_1 || A0_SYSERR(err) == syserr_2));
}

TEST_CASE("deadman] acquire, release") {
  a0_deadman_t d = A0_EMPTY;

  uint64_t tkn;
  REQUIRE_OK(a0_deadman_acquire(&d, &tkn));
  REQUIRE(tkn == 0);
  REQUIRE_OK(a0_deadman_release(&d));

  REQUIRE_OK(a0_deadman_acquire(&d, &tkn));
  REQUIRE(tkn == 1);
  REQUIRE_OK(a0_deadman_release(&d));
}

TEST_CASE("deadman] thread") {
  a0_deadman_t d = A0_EMPTY;
  a0::test::Event evt;

  bool acquired = false;
  REQUIRE_OK(a0_deadman_isacquired(&d, &acquired, nullptr));
  REQUIRE(!acquired);

  std::thread t([&]() {
    uint64_t tkn;
    REQUIRE_OK(a0_deadman_acquire(&d, &tkn));
    evt.wait();
    REQUIRE_OK(a0_deadman_release(&d));
  });

  uint64_t tkn;
  REQUIRE_OK(a0_deadman_wait_acquired(&d, &tkn));

  REQUIRE_OK(a0_deadman_isacquired(&d, &acquired, nullptr));
  REQUIRE(acquired);

  evt.set();
  REQUIRE_OK(a0_deadman_wait_released(&d, tkn));

  t.join();
}

TEST_CASE("deadman] death") {
  a0::test::IpcPool ipc_pool;
  auto* d = ipc_pool.make<a0_deadman_t>();

  REQUIRE_EXIT({
    uint64_t tkn;
    REQUIRE_OK(a0_deadman_acquire(d, &tkn));
  });

  uint64_t tkn;
  REQUIRE(a0_mtx_previous_owner_died(a0_deadman_acquire(d, &tkn)));
  REQUIRE(tkn == 1);
  REQUIRE_OK(a0_deadman_wait_released(d, tkn));
}

TEST_CASE("deadman] fuzz") {
  a0::test::IpcPool ipc_pool;
  auto* d = ipc_pool.make<a0_deadman_t>();
  auto* done = ipc_pool.make<bool>();
  auto* quick_exit_cnt = ipc_pool.make<std::atomic<uint64_t>>();

  std::vector<pid_t> children;

  for (int i = 0; i < 100; ++i) {
    children.push_back(a0::test::subproc([&] {
      while (!*done) {
        bool acquired = false;
        uint64_t tkn;

        int acquire_action = rand() % 4;
        if (acquire_action == 0) {
          a0_err_t err = a0_deadman_acquire(d, &tkn);
          REQUIRE_OK_OR_SYSERR(err, EOWNERDEAD);
          acquired = true;
        } else if (acquire_action == 1) {
          a0_err_t err = a0_deadman_tryacquire(d, &tkn);
          REQUIRE_OK_OR_SYSERR(err, EBUSY);
          acquired = !err;
        } else if (acquire_action == 2) {
          auto timeout = a0::test::timeout_in(std::chrono::microseconds(100));
          a0_err_t err = a0_deadman_timedacquire(d, &timeout, &tkn);
          REQUIRE_OK_OR_SYSERR(err, ETIMEDOUT);
          acquired = !err;
        } else if (acquire_action == 3) {
          REQUIRE_OK(a0_deadman_wait_acquired(d, &tkn));
          acquired = false;
        } else {
          REQUIRE(false);
        }

        if (acquired) {
          if (rand() % 100 == 0) {
            *quick_exit_cnt += 1;
            quick_exit(0);
          }
          std::this_thread::sleep_for(std::chrono::microseconds(10));
          REQUIRE_OK(a0_deadman_release(d));
        } else {
          int wait_release_action = rand() % 4;
          if (wait_release_action == 0) {
            a0_err_t err = a0_deadman_wait_released(d, tkn);
            REQUIRE_OK_OR_SYSERR(err, EOWNERDEAD);
          } else if (wait_release_action == 1) {
            a0_err_t err = a0_deadman_wait_released_any(d);
            REQUIRE_OK_OR_SYSERR(err, EOWNERDEAD);
          } else if (wait_release_action == 2) {
            auto timeout = a0::test::timeout_in(std::chrono::microseconds(100));
            a0_err_t err = a0_deadman_timedwait_released(d, &timeout, tkn);
            REQUIRE_OK_OR_SYSERR(err, EOWNERDEAD, ETIMEDOUT);
          } else if (wait_release_action == 3) {
            auto timeout = a0::test::timeout_in(std::chrono::microseconds(100));
            a0_err_t err = a0_deadman_timedwait_released_any(d, &timeout);
            REQUIRE_OK_OR_SYSERR(err, EOWNERDEAD, ETIMEDOUT);
          } else {
            REQUIRE(false);
          }
        }
      }
    }));
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::vector<pid_t> keep_children;
  for (auto& child : children) {
    if (rand() % 10 == 0) {
      kill(child, SIGKILL);
      int ret_code;
      waitpid(child, &ret_code, 0);
    } else {
      keep_children.push_back(child);
    }
  }
  children = std::move(keep_children);
  std::this_thread::sleep_for(std::chrono::seconds(1));

  *done = true;
  uint64_t tkn;
  REQUIRE_OK_OR_SYSERR(a0_deadman_acquire(d, &tkn), EOWNERDEAD);
  REQUIRE_OK(a0_deadman_release(d));

  for (auto& child : children) {
    REQUIRE_SUBPROC_EXITED(child);
  }
}