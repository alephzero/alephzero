#include <a0/discovery.h>
#include <a0/discovery.hpp>
#include <a0/file.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "src/test_util.hpp"

TEST_CASE("discovery] discovery") {
  try {
    a0::File::remove_all("/dev/shm/discovery_test/");
  } catch (...) {
  }
  a0::File("/dev/shm/discovery_test/unused");
  a0::File::remove("/dev/shm/discovery_test/unused");

  struct data_t {
    std::vector<std::string> paths;
    std::condition_variable cv;
    std::mutex mu;
  } data;

  a0_discovery_callback_t callback = {
      .user_data = &data,
      .fn = [](void* user_data, const char* path) {
        auto* data = (data_t*)user_data;
        std::unique_lock<std::mutex> lock(data->mu);
        data->paths.push_back(path);
        data->cv.notify_one();
      },
  };

  a0_discovery_t d;
  REQUIRE_OK(a0_discovery_init(&d, "/dev/shm/discovery_test/**/*.a0", callback));

  a0::File("/dev/shm/discovery_test/file.a0");
  a0::File("/dev/shm/discovery_test/a/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file3.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file4.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file5.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file6.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file7.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file8.a0");

  {
    std::unique_lock<std::mutex> lock(data.mu);
    data.cv.wait(lock, [&data] { return data.paths.size() >= 13; });
  }

  REQUIRE_OK(a0_discovery_close(&d));

  std::sort(data.paths.begin(), data.paths.end());
  REQUIRE(data.paths == std::vector<std::string>{
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file2.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file3.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file4.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file5.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file6.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file7.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file8.a0",
                            "/dev/shm/discovery_test/a/b/c/d/file.a0",
                            "/dev/shm/discovery_test/a/b/c/d/file2.a0",
                            "/dev/shm/discovery_test/a/b/file.a0",
                            "/dev/shm/discovery_test/a/file.a0",
                            "/dev/shm/discovery_test/file.a0",
                        });
}

TEST_CASE("discovery] cpp discovery") {
  try {
    a0::File::remove_all("/dev/shm/discovery_test/");
  } catch (...) {
  }
  a0::File("/dev/shm/discovery_test/unused");
  a0::File::remove("/dev/shm/discovery_test/unused");

  struct data_t {
    std::vector<std::string> paths;
    std::condition_variable cv;
    std::mutex mu;
  } data;

  a0::File("/dev/shm/discovery_test/file.a0");
  a0::File("/dev/shm/discovery_test/a/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/file.a0");

  a0::Discovery discovery(
      "/dev/shm/discovery_test/**/*.a0",
      [&](const std::string& path) {
        std::unique_lock<std::mutex> lock(data.mu);
        data.paths.push_back(path);
        data.cv.notify_one();
      });

  a0::File("/dev/shm/discovery_test/a/b/c/d/file1.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file1.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file3.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file4.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file5.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file6.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file7.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file8.a0");

  {
    std::unique_lock<std::mutex> lock(data.mu);
    data.cv.wait(lock, [&data] { return data.paths.size() >= 13; });
  }

  discovery = {};

  std::sort(data.paths.begin(), data.paths.end());
  REQUIRE(data.paths == std::vector<std::string>{
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file1.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file2.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file3.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file4.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file5.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file6.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file7.a0",
                            "/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file8.a0",
                            "/dev/shm/discovery_test/a/b/c/d/file1.a0",
                            "/dev/shm/discovery_test/a/b/c/d/file2.a0",
                            "/dev/shm/discovery_test/a/b/file.a0",
                            "/dev/shm/discovery_test/a/file.a0",
                            "/dev/shm/discovery_test/file.a0",
                        });
}

TEST_CASE("discovery] cpp discovery exact match") {
  try {
    a0::File::remove_all("/dev/shm/discovery_test/");
  } catch (...) {
  }
  a0::File("/dev/shm/discovery_test/unused");
  a0::File::remove("/dev/shm/discovery_test/unused");

  struct data_t {
    std::vector<std::string> paths;
    std::condition_variable cv;
    std::mutex mu;
  } data;

  a0::File("/dev/shm/discovery_test/file.a0");
  a0::File("/dev/shm/discovery_test/a/file.a0");
  a0::File("/dev/shm/discovery_test/a/b/file.a0");

  a0::Discovery discovery_before(
      "/dev/shm/discovery_test/a/file.a0",
      [&](const std::string& path) {
        std::unique_lock<std::mutex> lock(data.mu);
        data.paths.push_back(path);
        data.cv.notify_one();
      });

  a0::Discovery discovery_after(
      "/dev/shm/discovery_test/a/b/c/d/file1.a0",
      [&](const std::string& path) {
        std::unique_lock<std::mutex> lock(data.mu);
        data.paths.push_back(path);
        data.cv.notify_one();
      });

  a0::File("/dev/shm/discovery_test/a/b/c/d/file1.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file1.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file2.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file3.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file4.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file5.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file6.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file7.a0");
  a0::File("/dev/shm/discovery_test/a/b/c/d/e/f/g/h/i/j/k/l/m/file8.a0");

  {
    std::unique_lock<std::mutex> lock(data.mu);
    data.cv.wait(lock, [&data] { return data.paths.size() >= 2; });
  }

  discovery_before = {};
  discovery_after = {};

  std::sort(data.paths.begin(), data.paths.end());
  REQUIRE(data.paths == std::vector<std::string>{
                            "/dev/shm/discovery_test/a/b/c/d/file1.a0",
                            "/dev/shm/discovery_test/a/file.a0",
                        });
}
