#include <a0/deadman_mtx.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/mtx.h>
#include <a0/tid.h>

#include <doctest.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include "src/err_macro.h"
#include "src/test_util.hpp"
#include "src/tsan.h"

TEST_CASE("deadman_mtx] basic") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  a0_deadman_mtx_state_t state;

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(!state.is_locked);

  REQUIRE_OK(a0_deadman_mtx_lock(&d));

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(state.is_locked);
  REQUIRE(state.tkn == 1);
  REQUIRE(state.owner_tid == a0_tid());

  REQUIRE_OK(a0_deadman_mtx_unlock(&d));

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(!state.is_locked);
}

TEST_CASE("deadman_mtx] thread") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  a0_deadman_mtx_state_t state;
  uint64_t tkn;

  a0::test::Event evt;
  a0_tid_t thrd_id;

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(!state.is_locked);

  std::thread t([&]() {
    thrd_id = a0_tid();
    REQUIRE_OK(a0_deadman_mtx_lock(&d));
    evt.wait();
    REQUIRE_OK(a0_deadman_mtx_unlock(&d));
  });

  REQUIRE_OK(a0_deadman_mtx_wait_locked(&d, &tkn));

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(state.is_locked);
  REQUIRE(state.tkn == 1);
  REQUIRE(state.owner_tid == thrd_id);

  evt.set();
  REQUIRE_OK(a0_deadman_mtx_wait_unlocked(&d, tkn));

  t.join();
}

TEST_CASE("deadman_mtx] trylock success") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  a0_deadman_mtx_state_t state;

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(!state.is_locked);

  REQUIRE_OK(a0_deadman_mtx_trylock(&d));

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(state.is_locked);
  REQUIRE(state.tkn == 1);
  REQUIRE(state.owner_tid == a0_tid());

  REQUIRE_OK(a0_deadman_mtx_unlock(&d));

  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
  REQUIRE(!state.is_locked);
}

TEST_CASE("deadman_mtx] trylock failure") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;

  a0::test::Event evt;

  std::thread t([&]() {
    a0_deadman_mtx_t d;
    a0_deadman_mtx_init(&d, &stkn);

    REQUIRE_OK(a0_deadman_mtx_lock(&d));
    evt.wait();
    REQUIRE_OK(a0_deadman_mtx_unlock(&d));
  });

  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  uint64_t tkn;
  REQUIRE_OK(a0_deadman_mtx_wait_locked(&d, &tkn));
  REQUIRE(tkn == 1);

  REQUIRE(A0_SYSERR(a0_deadman_mtx_trylock(&d)) == EBUSY);

  evt.set();
  t.join();
}

TEST_CASE("deadman_mtx] death") {
  a0::test::IpcPool ipc_pool;
  auto* stkn = ipc_pool.make<a0_deadman_mtx_shared_token_t>();
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, stkn);

  REQUIRE_EXIT({
    REQUIRE_OK(a0_deadman_mtx_lock(&d));
  });

  REQUIRE(a0_mtx_previous_owner_died(a0_deadman_mtx_lock(&d)));
  REQUIRE_OK(a0_deadman_mtx_unlock(&d));
}

TEST_CASE("deadman_mtx] lock shutdown") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;

  a0_deadman_mtx_t d0;
  a0_deadman_mtx_init(&d0, &stkn);

  a0_deadman_mtx_t d1;
  a0_deadman_mtx_init(&d1, &stkn);

  REQUIRE_OK(a0_deadman_mtx_lock(&d0));

  std::thread t([&]() {
    REQUIRE(A0_SYSERR(a0_deadman_mtx_lock(&d1)) == ESHUTDOWN);
  });

  REQUIRE_OK(a0_deadman_mtx_shutdown(&d1));

  t.join();
  REQUIRE_OK(a0_deadman_mtx_unlock(&d0));
}

TEST_CASE("deadman_mtx] wait_locked success") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  a0::test::Event evt;

  std::thread t([&]() {
    REQUIRE_OK(a0_deadman_mtx_wait_locked(&d, nullptr));
    evt.set();
  });

  REQUIRE_OK(a0_deadman_mtx_lock(&d));
  evt.wait();
  REQUIRE_OK(a0_deadman_mtx_unlock(&d));
  t.join();
}

TEST_CASE("deadman_mtx] wait_locked shutdown") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  std::thread t([&]() {
    REQUIRE(A0_SYSERR(a0_deadman_mtx_wait_locked(&d, nullptr)) == ESHUTDOWN);
  });

  REQUIRE_OK(a0_deadman_mtx_shutdown(&d));
  t.join();
}

TEST_CASE("deadman_mtx] wait_unlocked success") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  bool complete = false;

  REQUIRE_OK(a0_deadman_mtx_lock(&d));

  a0_deadman_mtx_state_t state;
  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));

  std::thread t([&]() {
    REQUIRE_OK(a0_deadman_mtx_wait_unlocked(&d, state.tkn));
    A0_TSAN_HAPPENS_AFTER(&complete);
    complete = true;
  });

  REQUIRE(!complete);
  A0_TSAN_HAPPENS_BEFORE(&complete);
  REQUIRE_OK(a0_deadman_mtx_unlock(&d));
  t.join();
  REQUIRE(complete);
}

TEST_CASE("deadman_mtx] wait_unlocked shutdown") {
  a0_deadman_mtx_shared_token_t stkn = A0_EMPTY;
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, &stkn);

  bool complete = false;

  REQUIRE_OK(a0_deadman_mtx_lock(&d));

  a0_deadman_mtx_state_t state;
  REQUIRE_OK(a0_deadman_mtx_state(&d, &state));

  std::thread t([&]() {
    REQUIRE(A0_SYSERR(a0_deadman_mtx_wait_unlocked(&d, state.tkn)) == ESHUTDOWN);
    complete = true;
  });

  REQUIRE(!complete);
  REQUIRE_OK(a0_deadman_mtx_shutdown(&d));
  t.join();
  REQUIRE(complete);

  REQUIRE_OK(a0_deadman_mtx_unlock(&d));
}

TEST_CASE("deadman_mtx] fuzz") {
  a0::test::IpcPool ipc_pool;
  auto* stkn = ipc_pool.make<a0_deadman_mtx_shared_token_t>();
  auto* done = ipc_pool.make<bool>();

  std::vector<pid_t> children;
  for (int i = 0; i < 100; ++i) {
    children.push_back(a0::test::subproc([&] {
      a0_deadman_mtx_t d;
      a0_deadman_mtx_init(&d, stkn);

      while (!*done) {
        bool is_lock_owner = false;
        uint64_t tkn = 0;

        int lock_action = rand() % 4;
        if (lock_action == 0) {
          a0_err_t err = a0_deadman_mtx_lock(&d);
          REQUIRE(a0_mtx_lock_successful(err));
          is_lock_owner = true;
        } else if (lock_action == 1) {
          a0_err_t err = a0_deadman_mtx_trylock(&d);
          REQUIRE((a0_mtx_lock_successful(err) || A0_SYSERR(err) == EBUSY));
          is_lock_owner = a0_mtx_lock_successful(err);
        } else if (lock_action == 2) {
          auto timeout = a0::test::timeout_in(std::chrono::milliseconds(1));
          a0_err_t err = a0_deadman_mtx_timedlock(&d, &timeout);
          REQUIRE((a0_mtx_lock_successful(err) || A0_SYSERR(err) == ETIMEDOUT));
          is_lock_owner = a0_mtx_lock_successful(err);
        } else if (lock_action == 3) {
          REQUIRE_OK(a0_deadman_mtx_wait_locked(&d, &tkn));
          is_lock_owner = false;
        }

        if (is_lock_owner) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          if (rand() % 10 == 0) {
            quick_exit(0);
          }
          REQUIRE_OK(a0_deadman_mtx_unlock(&d));
        } else {
          if (!tkn) {
            a0_deadman_mtx_state_t state;
            REQUIRE_OK(a0_deadman_mtx_state(&d, &state));
            if (!state.is_locked) {
              continue;
            }
            tkn = state.tkn;
          }

          int wait_unlock_action = rand() % 2;
          if (wait_unlock_action == 0) {
            REQUIRE_OK(a0_deadman_mtx_wait_unlocked(&d, tkn));
          } else if (wait_unlock_action == 1) {
            auto timeout = a0::test::timeout_in(std::chrono::milliseconds(1));
            a0_err_t err = a0_deadman_mtx_timedwait_unlocked(&d, &timeout, tkn);
            REQUIRE((!err || A0_SYSERR(err) == ETIMEDOUT));
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
  a0_deadman_mtx_t d;
  a0_deadman_mtx_init(&d, stkn);
  REQUIRE(a0_mtx_lock_successful(a0_deadman_mtx_lock(&d)));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  REQUIRE_OK(a0_deadman_mtx_unlock(&d));

  for (auto& child : children) {
    REQUIRE_SUBPROC_EXITED(child);
  }
}