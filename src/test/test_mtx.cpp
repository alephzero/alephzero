#include <a0/arena.h>

#include <doctest.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

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
    REQUIRE_OK(a0_mtx_init(mtx));
    return mtx;
  }
};

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] lock, trylock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_mtx_trylock(mtx) == EBUSY);
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] (lock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE(a0_mtx_lock(mtx) == EDEADLK);
    REQUIRE_OK(a0_mtx_unlock(mtx));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] (lock, unlock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    for (int i = 0; i < 2; i++) {
      REQUIRE_OK(a0_mtx_lock(mtx));
      REQUIRE_OK(a0_mtx_unlock(mtx));
    }
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] unlock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE(a0_mtx_unlock(mtx) == EPERM);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] lock, (unlock)*") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE_OK(a0_mtx_lock(mtx));
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_unlock(mtx) == EPERM);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] consistent") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();
    REQUIRE(a0_mtx_consistent(mtx) == EINVAL);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] lock, lock2, unlock2, unlock") {
  REQUIRE_EXIT({
    auto* mtx1 = new_mtx();
    auto* mtx2 = new_mtx();

    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx1));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] lock, lock2, unlock, unlock2") {
  REQUIRE_EXIT({
    auto* mtx1 = new_mtx();
    auto* mtx2 = new_mtx();

    REQUIRE_OK(a0_mtx_lock(mtx1));
    REQUIRE_OK(a0_mtx_lock(mtx2));
    REQUIRE_OK(a0_mtx_unlock(mtx1));
    REQUIRE_OK(a0_mtx_unlock(mtx2));
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] unlock in wrong thread") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] trylock in different thread") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] robust chain") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] multiple waiters") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] owner died with lock, not consistent, lock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_lock(mtx) == ENOTRECOVERABLE);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] owner died with lock, consistent, lock") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] owner died with lock, not consistent, trylock") {
  REQUIRE_EXIT({
    auto* mtx = new_mtx();

    REQUIRE_EXIT({ REQUIRE_OK(a0_mtx_lock(mtx)); });

    REQUIRE(a0_mtx_lock(mtx) == EOWNERDEAD);
    REQUIRE_OK(a0_mtx_unlock(mtx));
    REQUIRE(a0_mtx_trylock(mtx) == ENOTRECOVERABLE);
  });
}

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] owner died with lock, consistent, trylock") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] fuzz (lock, unlock)") {
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

TEST_CASE_FIXTURE(MtxTestFixture, "file_sync] fuzz (trylock, unlock)") {
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
