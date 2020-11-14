#include <a0/arena.h>

#include <doctest.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "src/atomic.h"
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

  a0_mtx_t* new_mtx() {
    std::string name = "mtx_" + std::to_string(files.size()) + ".file";
    a0_file_remove(name.c_str());

    a0_file_t file;
    a0_file_options_t fileopt = A0_FILE_OPTIONS_DEFAULT;
    fileopt.create_options.size = sizeof(a0_mtx_t);
    REQUIRE_OK(a0_file_open(name.c_str(), &fileopt, &file));
    files.push_back(file);

    auto* mtx = (a0_mtx_t*)file.arena.ptr;
    return mtx;
  }

  a0_cnd_t* new_cnd() {
    std::string name = "cnd_" + std::to_string(files.size()) + ".file";
    a0_file_remove(name.c_str());

    a0_file_t file;
    a0_file_options_t fileopt = A0_FILE_OPTIONS_DEFAULT;
    fileopt.create_options.size = sizeof(a0_cnd_t);
    REQUIRE_OK(a0_file_open(name.c_str(), &fileopt, &file));
    files.push_back(file);

    auto* cnd = (a0_cnd_t*)file.arena.ptr;
    return cnd;
  }
};

// TODO: Test a0_mtx_lock_timeout

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, trylock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_mtx_trylock(mtx) == EBUSY);
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] (lock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_mtx_lock(mtx) == EDEADLK);
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] (lock, unlock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    for (int i = 0; i < 2; i++) {
      REQUIRE_OK(a0_mtx_lock(mtx));
      REQUIRE_OK(a0_mtx_unlock(mtx));
    }
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] unlock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE(a0_mtx_unlock(mtx) == EPERM);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, (unlock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_unlock(mtx) == EPERM);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] consistent") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE(a0_mtx_consistent(mtx) == EINVAL);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, lock2, unlock2, unlock") {
  REQUIRE_EXIT({
    auto* mtx1 = new_mtx();
    auto* mtx2 = new_mtx();

    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx1));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] lock, lock2, unlock, unlock2") {
  REQUIRE_EXIT({
    auto* mtx1 = new_mtx();
    auto* mtx2 = new_mtx();

    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx1));
    REQUIRE_OK(a0_mtx_unlock(mtx2));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] unlock in wrong thread") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    a0::Event event_0;
    a0::Event event_1;
    std::thread t([&]() {
      REQUIRE_OK(a0_mtx_lock(mtx));
      event_0.set();
      event_1.wait();
    });
    event_0.wait();
    REQUIRE(a0_mtx_unlock(mtx) == EPERM);
    event_1.set();

    t.join();
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] trylock in different thread") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

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
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] robust chain") {
  REQUIRE_EXIT({
    auto* mtx1 = new_mtx();
    auto* mtx2 = new_mtx();
    auto* mtx3 = new_mtx();

    REQUIRE_EXIT({
      REQUIRE_OK(a0_mtx_lock(mtx1));
      REQUIRE_OK(a0_mtx_lock(mtx2));
      REQUIRE_OK(a0_mtx_lock(mtx3));
    });

    REQUIRE(a0_mtx_lock(mtx1) == EOWNERDEAD);
    REQUIRE(a0_mtx_lock(mtx2) == EOWNERDEAD);
    REQUIRE(a0_mtx_lock(mtx3) == EOWNERDEAD);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] multiple waiters") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

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
      waitpid(child, nullptr, 0);
    }
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, not consistent, lock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_lock(mtx) == ENOTRECOVERABLE);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, consistent, lock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_consistent(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, not consistent, trylock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });
    
    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_trylock(mtx) == ENOTRECOVERABLE);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] owner died with lock, consistent, trylock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_consistent(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE_OK(a0_mtx_trylock(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] fuzz (lock, unlock)") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

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
      waitpid(child, nullptr, 0);
    }
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "mtx] fuzz (trylock, unlock)") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

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
      waitpid(child, nullptr, 0);
    }
  });
}

// errno_t a0_cnd_wait(a0_cnd_t*, a0_mtx_t*) A0_WARN_UNUSED_RESULT;
// errno_t a0_cnd_timedwait(a0_cnd_t*, a0_mtx_t*, const struct timespec*) A0_WARN_UNUSED_RESULT;
// errno_t a0_cnd_signal(a0_cnd_t*, a0_mtx_t*);
// errno_t a0_cnd_broadcast(a0_cnd_t*, a0_mtx_t*);

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] signal chain") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] signal chain\n");
    std::vector<std::thread> threads;
    auto* cnd = new_cnd();
    auto* mtx = new_mtx();
    size_t state = 0;

    for (size_t i = 0; i < 1000; i++) {
      threads.emplace_back([&, i]() {
        REQUIRE_OK(a0_mtx_lock(mtx));
        while (state != i) {
          REQUIRE_OK(a0_cnd_signal(cnd, mtx));
          REQUIRE_OK(a0_cnd_wait(cnd, mtx));
        }
        state = i + 1;
        REQUIRE_OK(a0_cnd_signal(cnd, mtx));
        REQUIRE_OK(a0_mtx_unlock(mtx));
      });
    }

    for (auto&& t : threads) {
      t.join();
    }

    REQUIRE(state == 1000);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] broadcast chain") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] broadcast chain\n");
    std::vector<std::thread> threads;
    auto* cnd = new_cnd();
    auto* mtx = new_mtx();
    size_t state = 0;

    for (size_t i = 0; i < 1000; i++) {
      threads.emplace_back([&, i]() {
        REQUIRE_OK(a0_mtx_lock(mtx));
        while (state != i) {
          REQUIRE_OK(a0_cnd_wait(cnd, mtx));
        }
        state = i + 1;
        REQUIRE_OK(a0_cnd_broadcast(cnd, mtx));
        REQUIRE_OK(a0_mtx_unlock(mtx));
      });
    }

    for (auto&& t : threads) {
      t.join();
    }

    REQUIRE(state == 1000);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] signal ping broadcast pong") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] signal ping broadcast pong\n");
    std::vector<std::thread> threads;
    auto* cnd_pre = new_cnd();
    auto* cnd_post = new_cnd();
    auto* mtx = new_mtx();
    size_t pre = 0;
    bool ready = false;
    size_t post = 0;

    for (size_t i = 0; i < 10; i++) {
      threads.emplace_back([&, i]() {
        REQUIRE_OK(a0_mtx_lock(mtx));
        pre++;
        REQUIRE_OK(a0_cnd_signal(cnd_pre, mtx));
        while (!ready) {
          REQUIRE_OK(a0_cnd_wait(cnd_post, mtx));
        }
        post++;
        REQUIRE_OK(a0_mtx_unlock(mtx));
      });
    }

    REQUIRE_OK(a0_mtx_lock(mtx));
    while (pre != 10) {
      REQUIRE_OK(a0_cnd_wait(cnd_pre, mtx));
    }
    REQUIRE(pre == 10);
    REQUIRE(post == 0);

    ready = true;
    REQUIRE_OK(a0_cnd_broadcast(cnd_post, mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));

    for (auto&& t : threads) {
      t.join();
    }

    REQUIRE(pre == 10);
    REQUIRE(post == 10);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] timeout zero") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] timeout zero\n");
    auto* cnd = new_cnd();
    auto* mtx = new_mtx();

    timespec_t wake_time;
    wake_time.tv_sec = 0;
    wake_time.tv_nsec = 0;

    auto start = std::chrono::steady_clock::now();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_cnd_timedwait(cnd, mtx, &wake_time) == ETIMEDOUT);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration_ms.count() < 1);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] timeout now") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] timeout now\n");
    auto* cnd = new_cnd();
    auto* mtx = new_mtx();

    timespec_t wake_time;
    REQUIRE_OK(clock_gettime(CLOCK_MONOTONIC, &wake_time));

    auto start = std::chrono::steady_clock::now();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_cnd_timedwait(cnd, mtx, &wake_time) == ETIMEDOUT);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration_ms.count() < 1);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "cnd] timeout future") {
  REQUIRE_EXIT({
    fprintf(stderr, "cnd] timeout future\n");
    auto* cnd = new_cnd();
    auto* mtx = new_mtx();

    timespec_t wake_time;
    REQUIRE_OK(clock_gettime(CLOCK_MONOTONIC, &wake_time));
    wake_time.tv_sec += 1;

    auto start = std::chrono::steady_clock::now();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_cnd_timedwait(cnd, mtx, &wake_time) == ETIMEDOUT);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration_ms.count() < 1100);
    REQUIRE(duration_ms.count() > 900);
  });
}