#include <a0/empty.h>
#include <a0/event.h>
#include <a0/latch.h>
#include <a0/mtx.h>
#include <a0/time.h>
#include <a0/unused.h>

#include <doctest.h>
#include <signal.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "src/err_macro.h"
#include "src/test_util.hpp"

TEST_CASE("mtx] lock, trylock") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE(A0_SYSERR(a0_mtx_trylock(&mtx)) == EBUSY);
  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE("mtx] (lock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE(A0_SYSERR(a0_mtx_lock(&mtx)) == EDEADLK);
  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE("mtx] (lock, unlock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  for (int i = 0; i < 2; i++) {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    REQUIRE_OK(a0_mtx_unlock(&mtx));
  }
}

TEST_CASE("mtx] unlock") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE(A0_SYSERR(a0_mtx_unlock(&mtx)) == EPERM);
}

TEST_CASE("mtx] lock, (unlock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));
  REQUIRE(A0_SYSERR(a0_mtx_unlock(&mtx)) == EPERM);
}

TEST_CASE("mtx] lock, lock2, unlock2, unlock") {
  a0_mtx_t mtx1 = A0_EMPTY;
  a0_mtx_t mtx2 = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx1));
  REQUIRE_OK(a0_mtx_lock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx1));
}

TEST_CASE("mtx] lock, lock2, unlock, unlock2") {
  a0_mtx_t mtx1 = A0_EMPTY;
  a0_mtx_t mtx2 = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx1));
  REQUIRE_OK(a0_mtx_lock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx1));
  REQUIRE_OK(a0_mtx_unlock(&mtx2));
}

TEST_CASE("mtx] unlock in wrong thread") {
  a0_mtx_t mtx = A0_EMPTY;

  a0_event_t event_0 = A0_EMPTY;
  a0_event_t event_1 = A0_EMPTY;
  std::thread t([&]() {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    a0_event_set(&event_0);
    a0_event_wait(&event_1);
  });
  a0_event_wait(&event_0);
  REQUIRE(A0_SYSERR(a0_mtx_unlock(&mtx)) == EPERM);
  a0_event_set(&event_1);

  t.join();
}

TEST_CASE("mtx] trylock in different thread") {
  a0::test::IpcPool ipc_pool;
  auto* mtx = ipc_pool.make<a0_mtx_t>();

  a0_event_t event_0 = A0_EMPTY;
  a0_event_t event_1 = A0_EMPTY;
  std::thread t([&]() {
    REQUIRE_OK(a0_mtx_lock(mtx));
    a0_event_set(&event_0);
    a0_event_wait(&event_1);
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
  a0_event_wait(&event_0);
  REQUIRE(A0_SYSERR(a0_mtx_trylock(mtx)) == EBUSY);
  a0_event_set(&event_1);

  t.join();
}

TEST_CASE("mtx] timedlock") {
  a0_mtx_t mtx = A0_EMPTY;

  auto start = std::chrono::steady_clock::now();
  REQUIRE_OK(a0_mtx_lock(&mtx));
  std::thread t([&]() {
    auto wake_time = a0::test::timeout_in(std::chrono::seconds(1));
    REQUIRE(A0_SYSERR(a0_mtx_timedlock(&mtx, &wake_time)) == ETIMEDOUT);
  });
  t.join();
  REQUIRE_OK(a0_mtx_unlock(&mtx));
  auto end = std::chrono::steady_clock::now();

  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  REQUIRE(duration_ms.count() < 1100);
  REQUIRE(duration_ms.count() > 900);
}

TEST_CASE("mtx] robust chain") {
  a0::test::IpcPool ipc_pool;
  auto* mtx1 = ipc_pool.make<a0_mtx_t>();
  auto* mtx2 = ipc_pool.make<a0_mtx_t>();
  auto* mtx3 = ipc_pool.make<a0_mtx_t>();

  REQUIRE_EXIT({
    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_lock(mtx3));
  });

  REQUIRE(A0_SYSERR(a0_mtx_lock(mtx1)) == EOWNERDEAD);
  REQUIRE(A0_SYSERR(a0_mtx_lock(mtx2)) == EOWNERDEAD);
  REQUIRE(A0_SYSERR(a0_mtx_lock(mtx3)) == EOWNERDEAD);

  REQUIRE_OK(a0_mtx_unlock(mtx1));
  REQUIRE_OK(a0_mtx_unlock(mtx2));
  REQUIRE_OK(a0_mtx_unlock(mtx3));
}

TEST_CASE("mtx] multiple waiters") {
  a0::test::IpcPool ipc_pool;
  auto* mtx = ipc_pool.make<a0_mtx_t>();

  REQUIRE_OK(a0_mtx_lock(mtx));

  std::vector<pid_t> children;
  for (int i = 0; i < 3; i++) {
    children.push_back(a0::test::subproc([&]() {
      REQUIRE_OK(a0_mtx_lock(mtx));
      REQUIRE_OK(a0_mtx_unlock(mtx));
    }));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  REQUIRE_OK(a0_mtx_unlock(mtx));

  for (auto&& child : children) {
    REQUIRE_SUBPROC_EXITED(child);
  }
}

TEST_CASE("mtx] fuzz (lock, unlock)") {
  a0::test::IpcPool ipc_pool;
  auto* mtx = ipc_pool.make<a0_mtx_t>();

  auto body = [&]() {
    auto prior_owner_died = a0_mtx_lock(mtx);
    A0_MAYBE_UNUSED(prior_owner_died);
    if (rand() % 2) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    REQUIRE_OK(a0_mtx_unlock(mtx));
  };

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(100);
  std::vector<pid_t> children;
  for (int i = 0; i < 100; i++) {
    children.push_back(a0::test::subproc([&]() {
      while (std::chrono::steady_clock::now() < end) {
        body();
      }
    }));
  }

  for (auto&& child : children) {
    REQUIRE_SUBPROC_EXITED(child);
  }
}

TEST_CASE("mtx] fuzz (trylock, unlock)") {
  a0::test::IpcPool ipc_pool;
  auto* mtx = ipc_pool.make<a0_mtx_t>();

  auto body = [&]() {
    auto err = a0_mtx_trylock(mtx);
    if (A0_SYSERR(err) != EBUSY) {
      REQUIRE_OK(a0_mtx_unlock(mtx));
    }
  };

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(100);
  std::vector<pid_t> children;
  for (int i = 0; i < 100; i++) {
    children.push_back(a0::test::subproc([&]() {
      while (std::chrono::steady_clock::now() < end) {
        body();
      }
    }));
  }

  for (auto&& child : children) {
    REQUIRE_SUBPROC_EXITED(child);
  }
}

TEST_CASE("cnd] simple signal wait") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx));

  std::thread t([&]() {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    REQUIRE_OK(a0_cnd_signal(&cnd, &mtx));
    REQUIRE_OK(a0_mtx_unlock(&mtx));
  });

  REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));

  t.join();
}

TEST_CASE("cnd] timeout fail") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx));

  auto wake_time = a0::test::timeout_now();
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  wake_time = a0::test::timeout_in(std::chrono::milliseconds(100));
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  wake_time = a0::test::timeout_in(std::chrono::milliseconds(-100));
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE("cnd] many waiters") {
  std::vector<std::thread> threads;
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  std::vector<std::unique_ptr<a0_latch_t>> latches;
  size_t num_threads = 1000;
  if (a0::test::is_valgrind()) {
    num_threads = 100;
  }

  for (size_t i = 0; i < num_threads; i++) {
    latches.emplace_back(new a0_latch_t);
    a0_latch_t* latch = latches.back().get();
    a0_latch_init(latch, 2);

    threads.emplace_back([&, latch]() {
      REQUIRE_OK(a0_mtx_lock(&mtx));
      a0_latch_arrive_and_wait(latch, 1);

      REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
      REQUIRE_OK(a0_mtx_unlock(&mtx));
    });

    a0_latch_arrive_and_wait(latch, 1);
  }

  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE_OK(a0_cnd_broadcast(&cnd, &mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));

  for (auto&& t : threads) {
    t.join();
  }
}

TEST_CASE("cnd] signal chain") {
  std::vector<std::thread> threads;
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;
  size_t state = 0;

  size_t num_threads = 1000;
  if (a0::test::is_valgrind()) {
    num_threads = 100;
  }

  for (size_t i = 0; i < num_threads; i++) {
    threads.emplace_back([&, i]() {
      REQUIRE_OK(a0_mtx_lock(&mtx));
      while (state != i) {
        REQUIRE_OK(a0_cnd_signal(&cnd, &mtx));
        REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
      }
      state = i + 1;
      REQUIRE_OK(a0_cnd_signal(&cnd, &mtx));
      REQUIRE_OK(a0_mtx_unlock(&mtx));
    });
  }

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(state == num_threads);
}

TEST_CASE("cnd] broadcast chain") {
  std::vector<std::thread> threads;
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;
  size_t state = 0;

  size_t num_threads = 1000;
  if (a0::test::is_valgrind()) {
    num_threads = 100;
  }

  for (size_t i = 0; i < num_threads; i++) {
    threads.emplace_back([&, i]() {
      REQUIRE_OK(a0_mtx_lock(&mtx));
      while (state != i) {
        REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
      }
      state = i + 1;
      REQUIRE_OK(a0_cnd_broadcast(&cnd, &mtx));
      REQUIRE_OK(a0_mtx_unlock(&mtx));
    });
  }

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(state == num_threads);
}

TEST_CASE("cnd] signal ping broadcast pong") {
  std::vector<std::thread> threads;
  a0_cnd_t cnd_pre = A0_EMPTY;
  a0_cnd_t cnd_post = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;
  size_t pre = 0;
  bool ready = false;
  size_t post = 0;

  for (size_t i = 0; i < 10; i++) {
    threads.emplace_back([&]() {
      REQUIRE_OK(a0_mtx_lock(&mtx));
      pre++;
      REQUIRE_OK(a0_cnd_signal(&cnd_pre, &mtx));
      while (!ready) {
        REQUIRE_OK(a0_cnd_wait(&cnd_post, &mtx));
      }
      post++;
      REQUIRE_OK(a0_mtx_unlock(&mtx));
    });
  }

  REQUIRE_OK(a0_mtx_lock(&mtx));
  while (pre != 10) {
    REQUIRE_OK(a0_cnd_wait(&cnd_pre, &mtx));
  }
  REQUIRE(pre == 10);
  REQUIRE(post == 0);

  ready = true;
  REQUIRE_OK(a0_cnd_broadcast(&cnd_post, &mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));

  for (auto&& t : threads) {
    t.join();
  }

  REQUIRE(pre == 10);
  REQUIRE(post == 10);
}

TEST_CASE("cnd] wait must lock") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  auto wake_time = a0::test::timeout_in(std::chrono::milliseconds(100));

  REQUIRE(A0_SYSERR(a0_cnd_wait(&cnd, &mtx)) == EPERM);
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == EPERM);

  REQUIRE_OK(a0_mtx_lock(&mtx));

  std::thread t([&]() {
    REQUIRE(A0_SYSERR(a0_cnd_wait(&cnd, &mtx)) == EPERM);
    REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == EPERM);
  });
  t.join();

  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE("cnd] timeout") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  auto start = std::chrono::steady_clock::now();
  REQUIRE_OK(a0_mtx_lock(&mtx));

  a0_time_mono_t wake_time = A0_EMPTY;
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == EINVAL);

  wake_time = a0::test::timeout_now();
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  wake_time = a0::test::timeout_in(std::chrono::seconds(-1));
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  wake_time = a0::test::timeout_in(std::chrono::seconds(1));
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(&cnd, &mtx, &wake_time)) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(&mtx));
  auto end = std::chrono::steady_clock::now();

  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  REQUIRE(duration_ms.count() < 1100);
  REQUIRE(duration_ms.count() > 900);
}

TEST_CASE("cnd] robust") {
  a0::test::IpcPool ipc_pool;
  auto* cnd = ipc_pool.make<a0_cnd_t>();
  auto* mtx = ipc_pool.make<a0_mtx_t>();

  a0_latch_t* latch = ipc_pool.make<a0_latch_t>();
  a0_latch_init(latch, 2);

  auto child = a0::test::subproc([&]() {
    REQUIRE_OK(a0_mtx_lock(mtx));
    a0_latch_arrive_and_wait(latch, 1);
    REQUIRE_OK(a0_cnd_wait(cnd, mtx));
  });
  REQUIRE(child > 0);

  a0_latch_arrive_and_wait(latch, 1);
  REQUIRE_OK(a0_mtx_lock(mtx));

  REQUIRE_OK(kill(child, SIGKILL));  // send kill command
  REQUIRE_SUBPROC_SIGNALED(child);

  auto wake_time = a0::test::timeout_now();
  REQUIRE(A0_SYSERR(a0_cnd_timedwait(cnd, mtx, &wake_time)) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(mtx));
}
