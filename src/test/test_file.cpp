#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/file.h>
#include <a0/file.hpp>

#include <doctest.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "src/test_util.hpp"

static const int REGULAR_FILE_MASK = 0x8000;

inline bool file_exists(const char* path) {
  stat_t s;
  return !stat(path, &s);
}

TEST_CASE("file] basic") {
  static const char* TEST_FILE = "/tmp/test.file";
  a0_file_remove(TEST_FILE);

  a0_file_t file;

  REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
  REQUIRE(!strcmp(file.path, TEST_FILE));
  REQUIRE(file.fd > 0);
  REQUIRE(file.stat.st_size == A0_FILE_OPTIONS_DEFAULT.create_options.size);
  REQUIRE(file.stat.st_mode == (REGULAR_FILE_MASK | A0_FILE_OPTIONS_DEFAULT.create_options.mode));
  REQUIRE(file.arena.buf.size == file.stat.st_size);
  REQUIRE_OK(a0_file_close(&file));
}

TEST_CASE("file] no override") {
  static const char* TEST_FILE = "/tmp/test.file";
  a0_file_remove(TEST_FILE);

  a0_file_t file;

  REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
  REQUIRE(!strcmp(file.path, TEST_FILE));
  REQUIRE(file.fd > 0);
  REQUIRE_OK(a0_file_close(&file));

  // Doesn't resize or change permissions.
  a0_file_options_t opt = A0_FILE_OPTIONS_DEFAULT;
  opt.create_options.size = 32 * 1024 * 1024;
  opt.create_options.mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
  REQUIRE_OK(a0_file_open(TEST_FILE, &opt, &file));
  REQUIRE(file.stat.st_size == A0_FILE_OPTIONS_DEFAULT.create_options.size);
  REQUIRE(file.stat.st_mode == (REGULAR_FILE_MASK | A0_FILE_OPTIONS_DEFAULT.create_options.mode));
  REQUIRE_OK(a0_file_close(&file));
}

TEST_CASE("file] bad size") {
  static const char* TEST_FILE = "/tmp/test.file";
  a0_file_remove(TEST_FILE);
  REQUIRE(!file_exists(TEST_FILE));

  a0_file_t file;
  a0_file_options_t opt = A0_FILE_OPTIONS_DEFAULT;

  // Too big.
  opt.create_options.size = std::numeric_limits<off_t>::max();
  errno_t err = a0_file_open(TEST_FILE, &opt, &file);
  REQUIRE((err == ENOMEM || err == EINVAL || err == EFBIG));
  REQUIRE(!file_exists(TEST_FILE));

  // Too small.
  opt.create_options.size = -1;
  REQUIRE(a0_file_open(TEST_FILE, &opt, &file) == EINVAL);
  REQUIRE(!file_exists(TEST_FILE));

  // Just right.
  opt.create_options.size = 16 * 1024;
  REQUIRE_OK(a0_file_open(TEST_FILE, &opt, &file));
  REQUIRE(file_exists(TEST_FILE));
  REQUIRE(!strcmp(file.path, TEST_FILE));
  REQUIRE(file.fd > 0);
  REQUIRE_OK(a0_file_close(&file));
}

TEST_CASE("file] double close") {
  static const char* TEST_FILE = "/tmp/test.file";
  a0_file_remove(TEST_FILE);

  a0_file_t file;
  REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
  REQUIRE_OK(a0_file_close(&file));
  REQUIRE(a0_file_close(&file) == EBADF);
}

TEST_CASE("file] make dir recursive") {
  static const char* TEST_DIR = "/tmp/a0dir/";
  static const char* TEST_FILE_0 = "/tmp/a0dir/d0/test.file";
  static const char* TEST_FILE_1 = "/tmp/a0dir/d1/test.file";
  static const char* TEST_FILE_2 = "/tmp/a0dir/d1/sub/test.file";
  a0_file_remove_all(TEST_DIR);

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat(TEST_FILE_0, &st) == -1);
    REQUIRE(stat(TEST_FILE_1, &st) == -1);
    REQUIRE(stat(TEST_FILE_2, &st) == -1);
  }

  a0_file_t file_0;
  REQUIRE_OK(a0_file_open(TEST_FILE_0, nullptr, &file_0));
  REQUIRE(!strcmp(file_0.path, TEST_FILE_0));
  REQUIRE(file_0.fd > 0);
  REQUIRE_OK(a0_file_close(&file_0));

  a0_file_t file_1;
  REQUIRE_OK(a0_file_open(TEST_FILE_1, nullptr, &file_1));
  REQUIRE(!strcmp(file_1.path, TEST_FILE_1));
  REQUIRE(file_1.fd > 0);
  REQUIRE_OK(a0_file_close(&file_1));

  a0_file_t file_2;
  REQUIRE_OK(a0_file_open(TEST_FILE_2, nullptr, &file_2));
  REQUIRE(!strcmp(file_2.path, TEST_FILE_2));
  REQUIRE(file_2.fd > 0);
  REQUIRE_OK(a0_file_close(&file_2));

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == 0);
    REQUIRE(stat(TEST_FILE_0, &st) == 0);
    REQUIRE(stat(TEST_FILE_1, &st) == 0);
    REQUIRE(stat(TEST_FILE_2, &st) == 0);
  }

  a0_file_remove_all(TEST_DIR);

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat(TEST_FILE_0, &st) == -1);
    REQUIRE(stat(TEST_FILE_1, &st) == -1);
    REQUIRE(stat(TEST_FILE_2, &st) == -1);
  }
}

TEST_CASE("file] relative to /dev/shm") {
  static const char* TEST_FILE_0 = "d0/test.file";
  static const char* TEST_FILE_1 = "d1/test.file";
  static const char* TEST_FILE_2 = "d1/sub/test.file";
  a0_file_remove_all("/dev/shm/d0");
  a0_file_remove_all("/dev/shm/d1");
  REQUIRE_OK(unsetenv("A0_ROOT"));

  {
    stat_t st;
    REQUIRE(stat("/dev/shm/d0/test.file", &st) == -1);
    REQUIRE(stat("/dev/shm/d1/test.file", &st) == -1);
    REQUIRE(stat("/dev/shm/d1/sub/test.file", &st) == -1);
  }

  a0_file_t file_0;
  REQUIRE_OK(a0_file_open(TEST_FILE_0, nullptr, &file_0));
  REQUIRE(!strcmp(file_0.path, "/dev/shm/d0/test.file"));
  REQUIRE(file_0.fd > 0);
  REQUIRE_OK(a0_file_close(&file_0));

  a0_file_t file_1;
  REQUIRE_OK(a0_file_open(TEST_FILE_1, nullptr, &file_1));
  REQUIRE(!strcmp(file_1.path, "/dev/shm/d1/test.file"));
  REQUIRE(file_1.fd > 0);
  REQUIRE_OK(a0_file_close(&file_1));

  a0_file_t file_2;
  REQUIRE_OK(a0_file_open(TEST_FILE_2, nullptr, &file_2));
  REQUIRE(!strcmp(file_2.path, "/dev/shm/d1/sub/test.file"));
  REQUIRE(file_2.fd > 0);
  REQUIRE_OK(a0_file_close(&file_2));

  {
    stat_t st;
    REQUIRE(stat("/dev/shm/d0/test.file", &st) == 0);
    REQUIRE(stat("/dev/shm/d1/test.file", &st) == 0);
    REQUIRE(stat("/dev/shm/d1/sub/test.file", &st) == 0);
  }

  a0_file_remove_all("/dev/shm/d0");
  a0_file_remove_all("/dev/shm/d1");

  {
    stat_t st;
    REQUIRE(stat("/dev/shm/d0/test.file", &st) == -1);
    REQUIRE(stat("/dev/shm/d1/test.file", &st) == -1);
    REQUIRE(stat("/dev/shm/d1/sub/test.file", &st) == -1);
  }
}

TEST_CASE("file] custom A0_ROOT") {
  static const char* TEST_DIR = "/tmp/a0dir";
  static const char* TEST_FILE_0 = "d0/test.file";
  static const char* TEST_FILE_1 = "d1/test.file";
  static const char* TEST_FILE_2 = "d1/sub/test.file";
  a0_file_remove_all(TEST_DIR);
  REQUIRE_OK(setenv("A0_ROOT", TEST_DIR, true));

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat(TEST_FILE_0, &st) == -1);
    REQUIRE(stat(TEST_FILE_1, &st) == -1);
    REQUIRE(stat(TEST_FILE_2, &st) == -1);
  }

  a0_file_t file_0;
  REQUIRE_OK(a0_file_open(TEST_FILE_0, nullptr, &file_0));
  REQUIRE(!strcmp(file_0.path, "/tmp/a0dir/d0/test.file"));
  REQUIRE(file_0.fd > 0);
  REQUIRE_OK(a0_file_close(&file_0));

  a0_file_t file_1;
  REQUIRE_OK(a0_file_open(TEST_FILE_1, nullptr, &file_1));
  REQUIRE(!strcmp(file_1.path, "/tmp/a0dir/d1/test.file"));
  REQUIRE(file_1.fd > 0);
  REQUIRE_OK(a0_file_close(&file_1));

  a0_file_t file_2;
  REQUIRE_OK(a0_file_open(TEST_FILE_2, nullptr, &file_2));
  REQUIRE(!strcmp(file_2.path, "/tmp/a0dir/d1/sub/test.file"));
  REQUIRE(file_2.fd > 0);
  REQUIRE_OK(a0_file_close(&file_2));

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d0/test.file", &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d1/test.file", &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d1/sub/test.file", &st) == 0);
  }

  a0_file_remove_all(TEST_DIR);

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d0/test.file", &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d1/test.file", &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d1/sub/test.file", &st) == -1);
  }

  REQUIRE_OK(unsetenv("A0_ROOT"));
}

TEST_CASE("file] custom A0_ROOT slash") {
  static const char* TEST_DIR = "/tmp/a0dir/";  // end slash
  static const char* TEST_FILE_0 = "d0/test.file";
  static const char* TEST_FILE_1 = "d1/test.file";
  static const char* TEST_FILE_2 = "d1/sub/test.file";
  a0_file_remove_all(TEST_DIR);
  REQUIRE_OK(setenv("A0_ROOT", TEST_DIR, true));

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat(TEST_FILE_0, &st) == -1);
    REQUIRE(stat(TEST_FILE_1, &st) == -1);
    REQUIRE(stat(TEST_FILE_2, &st) == -1);
  }

  a0_file_t file_0;
  REQUIRE_OK(a0_file_open(TEST_FILE_0, nullptr, &file_0));
  REQUIRE(!strcmp(file_0.path, "/tmp/a0dir//d0/test.file"));
  REQUIRE(file_0.fd > 0);
  REQUIRE_OK(a0_file_close(&file_0));

  a0_file_t file_1;
  REQUIRE_OK(a0_file_open(TEST_FILE_1, nullptr, &file_1));
  REQUIRE(!strcmp(file_1.path, "/tmp/a0dir//d1/test.file"));
  REQUIRE(file_1.fd > 0);
  REQUIRE_OK(a0_file_close(&file_1));

  a0_file_t file_2;
  REQUIRE_OK(a0_file_open(TEST_FILE_2, nullptr, &file_2));
  REQUIRE(!strcmp(file_2.path, "/tmp/a0dir//d1/sub/test.file"));
  REQUIRE(file_2.fd > 0);
  REQUIRE_OK(a0_file_close(&file_2));

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d0/test.file", &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d1/test.file", &st) == 0);
    REQUIRE(stat("/tmp/a0dir/d1/sub/test.file", &st) == 0);
  }

  a0_file_remove_all(TEST_DIR);

  {
    stat_t st;
    REQUIRE(stat(TEST_DIR, &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d0/test.file", &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d1/test.file", &st) == -1);
    REQUIRE(stat("/tmp/a0dir/d1/sub/test.file", &st) == -1);
  }

  REQUIRE_OK(unsetenv("A0_ROOT"));
}

TEST_CASE("file] readonly") {
  static const char* TEST_FILE = "/tmp/test.file";
  a0_file_remove(TEST_FILE);

  {
    a0_file_t file;
    REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
    REQUIRE(file.arena.buf.ptr[0] == 0);
    file.arena.buf.ptr[0] = 1;
    REQUIRE(file.arena.buf.ptr[0] == 1);
    REQUIRE_OK(a0_file_close(&file));
  }

  {
    a0_file_t file;
    REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
    REQUIRE(file.arena.buf.ptr[0] == 1);
    file.arena.buf.ptr[0] = 2;
    REQUIRE(file.arena.buf.ptr[0] == 2);
    REQUIRE_OK(a0_file_close(&file));
  }

  {
    a0_file_options_t opt = A0_FILE_OPTIONS_DEFAULT;
    opt.open_options.arena_mode = A0_ARENA_MODE_READONLY;

    a0_file_t file;
    REQUIRE_OK(a0_file_open(TEST_FILE, &opt, &file));
    REQUIRE(file.arena.buf.ptr[0] == 2);
    // Note: this 3 will not be written to the file because of readonly mode.
    file.arena.buf.ptr[0] = 3;
    REQUIRE(file.arena.buf.ptr[0] == 3);
    REQUIRE_OK(a0_file_close(&file));
  }

  {
    a0_file_t file;
    REQUIRE_OK(a0_file_open(TEST_FILE, nullptr, &file));
    REQUIRE(file.arena.buf.ptr[0] == 2);
    REQUIRE_OK(a0_file_close(&file));
  }

  // Change the file to read-only mode.
  REQUIRE_OK(chmod(TEST_FILE, S_IRUSR));

  // Note: the root user can open a read-only file with write permissions.
  if (getuid()) {
    a0_file_t file;
    REQUIRE(a0_file_open(TEST_FILE, nullptr, &file) == EACCES);
  }

  {
    a0_file_options_t opt = A0_FILE_OPTIONS_DEFAULT;
    opt.open_options.arena_mode = A0_ARENA_MODE_READONLY;

    a0_file_t file;
    REQUIRE_OK(a0_file_open(TEST_FILE, &opt, &file));
    REQUIRE_OK(a0_file_close(&file));
  }
}

TEST_CASE("file] cpp") {
  {
    a0::File("/tmp/cpp/a/test.file");
    a0::File("/tmp/cpp/a/b/test.file");
    a0::File("/tmp/cpp/a/b/c/test.file");
  }
  REQUIRE(file_exists("/tmp/cpp/a/test.file"));
  REQUIRE(file_exists("/tmp/cpp/a/b/test.file"));
  REQUIRE(file_exists("/tmp/cpp/a/b/c/test.file"));

  a0::File::remove_all("/tmp/cpp");

  REQUIRE(!file_exists("/tmp/cpp/a/test.file"));
  REQUIRE(!file_exists("/tmp/cpp/a/b/test.file"));
  REQUIRE(!file_exists("/tmp/cpp/a/b/c/test.file"));

  static const char* TEST_FILE = "/tmp/cpp/test.file";
  a0::File file(TEST_FILE);

  REQUIRE(file.path() == TEST_FILE);
  REQUIRE(file.size() == A0_FILE_OPTIONS_DEFAULT.create_options.size);
  REQUIRE(file.size() == a0::Buf(a0::Arena(file)).size());
  REQUIRE(file.size() == a0::Buf(file).size());
  REQUIRE(file.size() == ((a0::Arena)file).buf().size());
  REQUIRE(file.size() == ((a0::Buf)file).size());
  REQUIRE(file.size() == file.stat().st_size);
  REQUIRE(file.fd() > 0);

  const a0::File cfile = file;
  REQUIRE(cfile.size() == ((const a0::Buf)cfile).size());

  a0::Arena arena;
  {
    a0::File file2(TEST_FILE);
    arena = file2;
  }
  REQUIRE(file.size() == arena.buf().size());

  file = {};
  a0::File::remove(TEST_FILE);

  a0::File::Options opts = a0::File::Options::DEFAULT;
  opts.create_options.size = 32 * 1024 * 1024;

  file = a0::File(TEST_FILE, opts);
  REQUIRE(file.size() == 32 * 1024 * 1024);

  file = {};
  a0::File::remove(TEST_FILE);

  opts.create_options.size = std::numeric_limits<off_t>::max();

  try {
    file = a0::File(TEST_FILE, opts);
  } catch (const std::exception& e) {
    std::string err = e.what();
    REQUIRE((err == "Cannot allocate memory" ||
             err == "File too large" ||
             err == "Invalid argument" ||
             err == "Out of memory"));
  }

  file = {};
  a0::File::remove(TEST_FILE);

  opts.create_options.size = -1;

  REQUIRE_THROWS_WITH(
      [&]() { file = a0::File(TEST_FILE, opts); }(),
      "Invalid argument");

  REQUIRE_THROWS_WITH(
      [&]() {
        file = a0::File();
        file.size();
      }(),
      "AlephZero method called with NULL object: size_t a0::File::size() const");
}
