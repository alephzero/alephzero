#include <a0/discovery.h>
#include <a0/discovery.hpp>
#include <a0/file.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include "src/test_util.hpp"

TEST_CASE("discovery] pathglob split") {
  a0_pathglob_t pathglob;
  REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/**/abc*def/*.a0"));

  REQUIRE(pathglob.depth == 5);

  REQUIRE(a0::test::str(pathglob.parts[0].str) == "dev");
  REQUIRE(pathglob.parts[0].type == A0_PATHGLOB_PART_TYPE_VERBATIM);

  REQUIRE(a0::test::str(pathglob.parts[1].str) == "shm");
  REQUIRE(pathglob.parts[1].type == A0_PATHGLOB_PART_TYPE_VERBATIM);

  REQUIRE(a0::test::str(pathglob.parts[2].str) == "**");
  REQUIRE(pathglob.parts[2].type == A0_PATHGLOB_PART_TYPE_RECURSIVE);

  REQUIRE(a0::test::str(pathglob.parts[3].str) == "abc*def");
  REQUIRE(pathglob.parts[3].type == A0_PATHGLOB_PART_TYPE_PATTERN);

  REQUIRE(a0::test::str(pathglob.parts[4].str) == "*.a0");
  REQUIRE(pathglob.parts[4].type == A0_PATHGLOB_PART_TYPE_PATTERN);

  REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/*.a0"));

  REQUIRE(pathglob.depth == 3);
  REQUIRE(a0::test::str(pathglob.parts[0].str) == "dev");
  REQUIRE(a0::test::str(pathglob.parts[1].str) == "shm");
  REQUIRE(a0::test::str(pathglob.parts[2].str) == "*.a0");

  REQUIRE_OK(a0_pathglob_init(&pathglob, "/*.a0"));

  REQUIRE(pathglob.depth == 1);
  REQUIRE(a0::test::str(pathglob.parts[0].str) == "*.a0");

  REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/"));

  REQUIRE(pathglob.depth == 3);
  REQUIRE(a0::test::str(pathglob.parts[0].str) == "dev");
  REQUIRE(a0::test::str(pathglob.parts[1].str) == "shm");
  REQUIRE(a0::test::str(pathglob.parts[2].str) == "");
}

TEST_CASE("discovery] pathglob match") {
  bool match;
  a0_pathglob_t glob;

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/a/foo.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(!match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/*/foo.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(!match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/*/*.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(!match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/*.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/b/*.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
  REQUIRE(!match);

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/**/**/**/**/*******b***/*.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
  REQUIRE(match);

  REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/*.a0"));

  REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/foo.a0", &match));
  REQUIRE(match);
}

TEST_CASE("discovery] cpp pathglob match") {
  a0::PathGlob glob;

  glob = a0::PathGlob("/dev/shm/a/foo.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/*/foo.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/*/*.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/*.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/b/*.a0");
  REQUIRE(!glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**");
  REQUIRE(glob.match("/dev/shm/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/**/**/**/**/*******b***/*.a0");
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/*.a0");
  REQUIRE(glob.match("/dev/shm/foo.a0"));
}

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
