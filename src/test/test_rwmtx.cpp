#include <a0/empty.h>
#include <a0/mtx.h>
#include <a0/rwmtx.h>
#include <a0/time.hpp>

#include <doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/err_macro.h"
#include "src/test_util.hpp"

struct RwmtxTestFixture {
  a0_rwmtx_t rwmtx = A0_EMPTY;
  a0_mtx_t _slots[4] = A0_EMPTY;
  a0_rwmtx_rmtx_span_t rmtx_span;

  std::vector<std::string> history;
  std::mutex history_mtx;

  std::chrono::nanoseconds short_sleep{std::chrono::milliseconds(10)};
  std::chrono::nanoseconds long_sleep{std::chrono::milliseconds(50)};

  RwmtxTestFixture() {
    rmtx_span = {_slots, 4};
  }

  void push_history(std::string str) {
    std::unique_lock<std::mutex> lk{history_mtx};
    history.push_back(std::move(str));
  }
};

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] basic wlock") {
  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] basic rlock") {
  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] basic trywlock") {
  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_trywlock(&rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] basic tryrlock") {
  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_tryrlock(&rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] multiple rlock") {
  a0::test::Latch latch(2);

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_0;
    REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_0));
    latch.arrive_and_wait();
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));
  });

  a0_rwmtx_tkn_t tkn_1;
  REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_1));
  latch.arrive_and_wait();
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_1));

  t.join();
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] wlock rlock") {
  a0_rwmtx_tkn_t tkn_w;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_w));
  push_history("w_lock");

  std::vector<std::thread> threads;
  for (size_t i = 0; i < 3; i++) {
    threads.emplace_back([&]() {
      a0_rwmtx_tkn_t tkn_r;
      REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));
      push_history("r_lock");
      std::this_thread::sleep_for(long_sleep);
      push_history("r_unlock");
      REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));
    });
  }

  std::this_thread::sleep_for(long_sleep);
  push_history("w_unlock");
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_w));

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(history == std::vector<std::string>{
      "w_lock", "w_unlock",
      "r_lock", "r_lock", "r_lock",
      "r_unlock", "r_unlock", "r_unlock"});
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] rlock wlock") {
  a0_rwmtx_tkn_t tkn_r;
  REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));
  push_history("r_lock");

  std::vector<std::thread> threads;
  for (size_t i = 0; i < 3; i++) {
    threads.emplace_back([&]() {
      a0_rwmtx_tkn_t tkn_w;
      REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_w));
      push_history("w_lock");
      std::this_thread::sleep_for(long_sleep);
      push_history("w_unlock");
      REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_w));
    });
  }

  std::this_thread::sleep_for(long_sleep);
  push_history("r_unlock");
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(history == std::vector<std::string>{
      "r_lock", "r_unlock",
      "w_lock", "w_unlock",
      "w_lock", "w_unlock",
      "w_lock", "w_unlock"});
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] rlock more than slots") {
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 6; i++) {
    threads.emplace_back([&, i]() {
      a0_rwmtx_tkn_t tkn;
      REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn));
      push_history("lock");
      std::this_thread::sleep_for(std::chrono::milliseconds(50 * (i + 1)));
      push_history("unlock");
      REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
    });
  }

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(history == std::vector<std::string>{
      "lock", "lock", "lock", "lock",
      "unlock", "lock",
      "unlock", "lock",
      "unlock", "unlock", "unlock", "unlock"});
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] tryrlock more than slots") {
  std::atomic<size_t> lock_cnt{0};
  std::atomic<size_t> nolock_cnt{0};

  std::vector<std::thread> threads;
  for (size_t i = 0; i < 6; i++) {
    threads.emplace_back([&, i]() {
      a0_rwmtx_tkn_t tkn;
      if (!a0_rwmtx_tryrlock(&rwmtx, rmtx_span, &tkn)) {
        lock_cnt++;
        std::this_thread::sleep_for(long_sleep);
        REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn));
      } else {
        nolock_cnt++;
      }
    });
  }

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(lock_cnt == 4);
  REQUIRE(nolock_cnt == 2);
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] wlock trywlock") {
  a0_rwmtx_tkn_t tkn_0;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_0));

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_1;
    REQUIRE(A0_SYSERR(a0_rwmtx_trywlock(&rwmtx, rmtx_span, &tkn_1))
            == EBUSY);
  });
  t.join();

  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] timedwlock success") {
  a0_rwmtx_tkn_t tkn_0;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_0));

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_1;
    REQUIRE_OK(a0_rwmtx_timedwlock(&rwmtx, rmtx_span, a0::test::timeout_in(long_sleep), &tkn_1));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_1));
  });

  std::this_thread::sleep_for(short_sleep);
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));

  t.join();
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] timedwlock timeout") {
  a0_rwmtx_tkn_t tkn_0;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_0));

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_1;
    REQUIRE(A0_SYSERR(a0_rwmtx_timedwlock(&rwmtx, rmtx_span, a0::test::timeout_in(short_sleep), &tkn_1))
            == ETIMEDOUT);
  });

  std::this_thread::sleep_for(long_sleep);
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));

  t.join();
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] rlocks, then wlock") {
  for (size_t i = 0; i < 6; i++) {
    a0_rwmtx_tkn_t tkn_r;
    REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));
  }
  a0_rwmtx_tkn_t tkn_w;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_w));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_w));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] rlocks, then trywlock") {
  for (size_t i = 0; i < 6; i++) {
    a0_rwmtx_tkn_t tkn_r;
    REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));
  }
  a0_rwmtx_tkn_t tkn_w;
  REQUIRE_OK(a0_rwmtx_trywlock(&rwmtx, rmtx_span, &tkn_w));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_w));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] rlocks, then timedwlock") {
  for (size_t i = 0; i < 6; i++) {
    a0_rwmtx_tkn_t tkn_r;
    REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));
  }
  a0_rwmtx_tkn_t tkn_w;
  REQUIRE_OK(a0_rwmtx_trywlock(&rwmtx, rmtx_span, &tkn_w));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_w));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwmtx] trywlock with active rlock") {
  a0_rwmtx_tkn_t tkn_r;
  REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_r));

  a0_rwmtx_tkn_t tkn_w;
  REQUIRE(A0_SYSERR(a0_rwmtx_trywlock(&rwmtx, rmtx_span, &tkn_w))
          == EBUSY);

  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_r));
}

TEST_CASE("rwmtx] wlock died") {
  a0::test::IpcPool ipc_pool;
  auto* rwmtx = ipc_pool.make<a0_rwmtx_t>();
  auto* _slots = ipc_pool.make<std::array<a0_mtx_t, 4>>();
  a0_rwmtx_rmtx_span_t rmtx_span = {_slots->data(), 4};

  REQUIRE_EXIT({
    a0_rwmtx_tkn_t tkn;
    REQUIRE_OK(a0_rwmtx_wlock(rwmtx, rmtx_span, &tkn));
  });

  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_wlock(rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(rwmtx, tkn));
}

TEST_CASE("rwmtx] rlock died") {
  a0::test::IpcPool ipc_pool;
  auto* rwmtx = ipc_pool.make<a0_rwmtx_t>();
  auto* _slots = ipc_pool.make<std::array<a0_mtx_t, 4>>();
  a0_rwmtx_rmtx_span_t rmtx_span = {_slots->data(), 4};

  REQUIRE_EXIT({
    a0_rwmtx_tkn_t tkn;
    REQUIRE_OK(a0_rwmtx_rlock(rwmtx, rmtx_span, &tkn));
  });

  a0_rwmtx_tkn_t tkn;
  REQUIRE_OK(a0_rwmtx_wlock(rwmtx, rmtx_span, &tkn));
  REQUIRE_OK(a0_rwmtx_unlock(rwmtx, tkn));
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwcnd] simple signal wait wlock") {
  a0_rwcnd_t rwcnd = A0_EMPTY;

  a0_rwmtx_tkn_t tkn_0;
  REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_0));

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_1;
    REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_1));
    REQUIRE_OK(a0_rwcnd_signal(&rwcnd));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_1));
  });

  REQUIRE_OK(a0_rwcnd_wait(&rwcnd, &rwmtx, rmtx_span, &tkn_0));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));

  t.join();
}

TEST_CASE_FIXTURE(RwmtxTestFixture, "rwcnd] simple signal wait rlock") {
  a0_rwcnd_t rwcnd = A0_EMPTY;

  a0_rwmtx_tkn_t tkn_0;
  REQUIRE_OK(a0_rwmtx_rlock(&rwmtx, rmtx_span, &tkn_0));

  std::thread t([&]() {
    a0_rwmtx_tkn_t tkn_1;
    REQUIRE_OK(a0_rwmtx_wlock(&rwmtx, rmtx_span, &tkn_1));
    REQUIRE_OK(a0_rwcnd_signal(&rwcnd));
    REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_1));
  });

  REQUIRE_OK(a0_rwcnd_wait(&rwcnd, &rwmtx, rmtx_span, &tkn_0));
  REQUIRE_OK(a0_rwmtx_unlock(&rwmtx, tkn_0));

  t.join();
}
