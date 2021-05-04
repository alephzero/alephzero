#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/file.h>
#include <a0/time.h>

#include <doctest.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/empty.h"
#include "src/mtx.h"
#include "src/sync.hpp"
#include "src/test_util.hpp"

struct MtxTestFixture {
  std::vector<a0_file_t> files;

  MtxTestFixture() = default;

  ~MtxTestFixture() {
    for (auto&& file : files) {
      a0_file_close(&file);
      a0_file_remove(file.path);
    }
  }

  uint8_t* ipc_buffer(uint32_t size) {
    std::string name = "alephzero_test/mtx/buf_" + std::to_string(files.size());
    a0_file_remove(name.c_str());

    a0_file_t file;
    a0_file_options_t fileopt = A0_FILE_OPTIONS_DEFAULT;
    fileopt.create_options.size = size;
    REQUIRE_OK(a0_file_open(name.c_str(), &fileopt, &file));
    files.push_back(file);

    return file.arena.buf.ptr;
  }

  template <typename T, typename... Args>
  T* make_ipc(Args&&... args) {
    auto* buf = ipc_buffer(sizeof(T));
    return new (buf) T(std::forward<Args>(args)...);
  }

  a0_time_mono_t delay(int64_t add_ns) {
    a0_time_mono_t now;
    REQUIRE_OK(a0_time_mono_now(&now));
    a0_time_mono_t target;
    REQUIRE_OK(a0_time_mono_add(now, add_ns, &target));
    return target;
  }
};

class latch_t {
  int32_t val;
  a0_mtx_t mtx = A0_EMPTY;
  a0_cnd_t cnd = A0_EMPTY;

 public:
  explicit latch_t(int32_t init_val)
      : val{init_val} {}

  void arrive_and_wait(int32_t update = 1) {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    val -= update;
    if (val <= 0) {
      REQUIRE_OK(a0_cnd_broadcast(&cnd, &mtx));
    }
    while (val > 0) {
      REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
    }
    REQUIRE_OK(a0_mtx_unlock(&mtx));
  }
};

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, trylock") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE(a0_mtx_trylock(&mtx) == EBUSY);
  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] (lock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE(a0_mtx_lock(&mtx) == EDEADLK);
  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] (lock, unlock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  for (int i = 0; i < 2; i++) {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    REQUIRE_OK(a0_mtx_unlock(&mtx));
  }
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] unlock") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE(a0_mtx_unlock(&mtx) == EPERM);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, (unlock)*") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));
  REQUIRE(a0_mtx_unlock(&mtx) == EPERM);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] consistent") {
  a0_mtx_t mtx = A0_EMPTY;
  REQUIRE(a0_mtx_consistent(&mtx) == EINVAL);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, lock2, unlock2, unlock") {
  a0_mtx_t mtx1 = A0_EMPTY;
  a0_mtx_t mtx2 = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx1));
  REQUIRE_OK(a0_mtx_lock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx1));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, lock2, unlock, unlock2") {
  a0_mtx_t mtx1 = A0_EMPTY;
  a0_mtx_t mtx2 = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx1));
  REQUIRE_OK(a0_mtx_lock(&mtx2));
  REQUIRE_OK(a0_mtx_unlock(&mtx1));
  REQUIRE_OK(a0_mtx_unlock(&mtx2));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] unlock in wrong thread") {
  a0_mtx_t mtx = A0_EMPTY;

  a0::Event event_0;
  a0::Event event_1;
  std::thread t([&]() {
    REQUIRE_OK(a0_mtx_lock(&mtx));
    event_0.set();
    event_1.wait();
  });
  event_0.wait();
  REQUIRE(a0_mtx_unlock(&mtx) == EPERM);
  event_1.set();

  t.join();
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] trylock in different thread") {
  auto* mtx = make_ipc<a0_mtx_t>();

  a0::Event event_0;
  a0::Event event_1;
  std::thread t([&]() {
    REQUIRE_OK(a0_mtx_lock(mtx));
    event_0.set();
    event_1.wait();
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
  event_0.wait();
  REQUIRE(a0_mtx_trylock(mtx) == EBUSY);
  event_1.set();

  t.join();
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] timedlock") {
  a0_mtx_t mtx = A0_EMPTY;

  auto start = std::chrono::steady_clock::now();
  REQUIRE_OK(a0_mtx_lock(&mtx));
  std::thread t([&]() {
    auto wake_time = delay(1e9);
    REQUIRE(a0_mtx_timedlock(&mtx, wake_time) == ETIMEDOUT);
  });
  t.join();
  REQUIRE_OK(a0_mtx_unlock(&mtx));
  auto end = std::chrono::steady_clock::now();

  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  REQUIRE(duration_ms.count() < 1100);
  REQUIRE(duration_ms.count() > 900);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] consistent call must be from owner") {
  auto* mtx = make_ipc<a0_mtx_t>();
  REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

  a0::Event event_0;
  a0::Event event_1;
  std::thread t([&]() {
    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    event_0.set();
    event_1.wait();
    REQUIRE_OK(a0_mtx_consistent(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
  event_0.wait();
  REQUIRE(a0_mtx_consistent(mtx) == EPERM);
  event_1.set();
  t.join();
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] robust chain") {
  auto* mtx1 = make_ipc<a0_mtx_t>();
  auto* mtx2 = make_ipc<a0_mtx_t>();
  auto* mtx3 = make_ipc<a0_mtx_t>();

  REQUIRE_EXIT({
    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_lock(mtx3));
  });

  REQUIRE(a0_mtx_lock(mtx1) == EOWNERDEAD);
  REQUIRE(a0_mtx_lock(mtx2) == EOWNERDEAD);
  REQUIRE(a0_mtx_lock(mtx3) == EOWNERDEAD);

  REQUIRE_OK(a0_mtx_consistent(mtx1));
  REQUIRE_OK(a0_mtx_consistent(mtx2));
  REQUIRE_OK(a0_mtx_consistent(mtx3));

  REQUIRE_OK(a0_mtx_unlock(mtx1));
  REQUIRE_OK(a0_mtx_unlock(mtx2));
  REQUIRE_OK(a0_mtx_unlock(mtx3));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] multiple waiters") {
  auto* mtx = make_ipc<a0_mtx_t>();

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

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, not consistent, lock") {
  auto* mtx = make_ipc<a0_mtx_t>();

  REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

  REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
  REQUIRE_OK(a0_mtx_unlock(mtx));
  REQUIRE(a0_mtx_lock(mtx) == ENOTRECOVERABLE);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, consistent, lock") {
  auto* mtx = make_ipc<a0_mtx_t>();

  REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

  REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
  REQUIRE_OK(a0_mtx_consistent(mtx));
  REQUIRE_OK(a0_mtx_unlock(mtx));
  REQUIRE_OK(a0_mtx_lock(mtx));
  REQUIRE_OK(a0_mtx_unlock(mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, not consistent, trylock") {
  auto* mtx = make_ipc<a0_mtx_t>();

  REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

  REQUIRE(a0_mtx_trylock(mtx) == EOWNERDEAD);
  REQUIRE_OK(a0_mtx_unlock(mtx));
  REQUIRE(a0_mtx_trylock(mtx) == ENOTRECOVERABLE);
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, consistent, trylock") {
  auto* mtx = make_ipc<a0_mtx_t>();

  REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

  REQUIRE(a0_mtx_trylock(mtx) == EOWNERDEAD);
  REQUIRE_OK(a0_mtx_consistent(mtx));
  REQUIRE_OK(a0_mtx_unlock(mtx));
  REQUIRE_OK(a0_mtx_trylock(mtx));
  REQUIRE_OK(a0_mtx_unlock(mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] fuzz (lock, unlock)") {
  auto* mtx = make_ipc<a0_mtx_t>();

  auto body = [&]() {
    auto err = a0_mtx_lock(mtx);
    if (err == EOWNERDEAD) {
      REQUIRE_OK(a0_mtx_consistent(mtx));
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

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] fuzz (trylock, unlock)") {
  auto* mtx = make_ipc<a0_mtx_t>();

  auto body = [&]() {
    auto err = a0_mtx_trylock(mtx);
    if (err != EBUSY) {
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

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] simple signal wait") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] timeout fail") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  REQUIRE_OK(a0_mtx_lock(&mtx));

  auto wake_time = delay(0);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  wake_time = delay(0.1 * 1e9);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  wake_time = delay(-0.1 * 1e9);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] many waiters") {
  std::vector<std::thread> threads;
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  std::vector<std::unique_ptr<latch_t>> latches;
  size_t num_threads = 1000;
  if (a0::test::is_valgrind()) {
    num_threads = 100;
  }

  for (size_t i = 0; i < num_threads; i++) {
    latches.push_back(std::make_unique<latch_t>(2));
    latch_t* latch = latches.back().get();

    threads.emplace_back([&, latch]() {
      REQUIRE_OK(a0_mtx_lock(&mtx));
      latch->arrive_and_wait();

      REQUIRE_OK(a0_cnd_wait(&cnd, &mtx));
      REQUIRE_OK(a0_mtx_unlock(&mtx));
    });

    latch->arrive_and_wait();
  }

  REQUIRE_OK(a0_mtx_lock(&mtx));
  REQUIRE_OK(a0_cnd_broadcast(&cnd, &mtx));
  REQUIRE_OK(a0_mtx_unlock(&mtx));

  for (auto&& t : threads) {
    t.join();
  }
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] signal chain") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] broadcast chain") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] signal ping broadcast pong") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] wait must lock") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  auto wake_time = delay(0.1 * 1e9);

  REQUIRE(a0_cnd_wait(&cnd, &mtx) == EPERM);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == EPERM);

  REQUIRE_OK(a0_mtx_lock(&mtx));

  std::thread t([&]() {
    REQUIRE(a0_cnd_wait(&cnd, &mtx) == EPERM);
    REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == EPERM);
  });
  t.join();

  REQUIRE_OK(a0_mtx_unlock(&mtx));
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] timeout") {
  a0_cnd_t cnd = A0_EMPTY;
  a0_mtx_t mtx = A0_EMPTY;

  auto start = std::chrono::steady_clock::now();
  REQUIRE_OK(a0_mtx_lock(&mtx));

  a0_time_mono_t wake_time = A0_EMPTY;
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == EINVAL);

  wake_time = delay(0);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  wake_time = delay(-1e9);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  wake_time = delay(1e9);
  REQUIRE(a0_cnd_timedwait(&cnd, &mtx, wake_time) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(&mtx));
  auto end = std::chrono::steady_clock::now();

  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  REQUIRE(duration_ms.count() < 1100);
  REQUIRE(duration_ms.count() > 900);
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] robust") {
  auto* cnd = make_ipc<a0_cnd_t>();
  auto* mtx = make_ipc<a0_mtx_t>();

  latch_t* latch = make_ipc<latch_t>(2);

  auto child = a0::test::subproc([&]() {
    REQUIRE_OK(a0_mtx_lock(mtx));
    latch->arrive_and_wait();
    REQUIRE_OK(a0_cnd_wait(cnd, mtx));
  });
  REQUIRE(child > 0);

  latch->arrive_and_wait();
  REQUIRE_OK(a0_mtx_lock(mtx));

  REQUIRE_OK(kill(child, SIGKILL));  // send kill command
  REQUIRE_SUBPROC_SIGNALED(child);

  auto wake_time = delay(0);
  REQUIRE(a0_cnd_timedwait(cnd, mtx, wake_time) == ETIMEDOUT);

  REQUIRE_OK(a0_mtx_unlock(mtx));
}
