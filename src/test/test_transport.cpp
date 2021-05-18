#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/file.h>
#include <a0/transport.h>

#include <doctest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "src/test_util.hpp"
#include "src/transport_debug.h"

static const char TEST_DISK[] = "/tmp/transport_test.a0";
static const char TEST_SHM[] = "transport_test.a0";
static const char TEST_SHM_ABS[] = "/dev/shm/transport_test.a0";

static const char COPY_DISK[] = "/tmp/copy.a0";
static const char COPY_SHM[] = "copy.a0";
static const char COPY_SHM_ABS[] = "/dev/shm/copy.a0";

struct StreamTestFixture {
  std::vector<uint8_t> stack_arena_data;
  a0_arena_t arena;

  a0_file_options_t diskopt;
  a0_file_t disk;

  a0_file_options_t shmopt;
  a0_file_t shm;

  StreamTestFixture() {
    stack_arena_data.resize(4096);
    arena = a0_arena_t{
        .buf = {
            .ptr = stack_arena_data.data(),
            .size = stack_arena_data.size(),
        },
        .mode = A0_ARENA_MODE_SHARED,
    };

    a0_file_remove(TEST_DISK);
    diskopt = A0_FILE_OPTIONS_DEFAULT;
    diskopt.create_options.size = 4096;
    a0_file_open(TEST_DISK, &diskopt, &disk);

    a0_file_remove(TEST_SHM);
    shmopt = A0_FILE_OPTIONS_DEFAULT;
    shmopt.create_options.size = 4096;
    a0_file_open(TEST_SHM, &shmopt, &shm);
  }

  ~StreamTestFixture() {
    a0_file_close(&disk);
    a0_file_remove(TEST_DISK);

    a0_file_close(&shm);
    a0_file_remove(TEST_SHM);
  }

  void require_debugstr(a0_locked_transport_t lk, const std::string& expected) {
    a0_buf_t debugstr;
    a0_transport_debugstr(lk, &debugstr);
    REQUIRE(a0::test::str(debugstr) == expected);
    free(debugstr.ptr);
  }
};

TEST_CASE_FIXTURE(StreamTestFixture, "transport] construct") {
  a0_transport_t transport;

  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 144
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 144
    }
  },
  "data": [
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] alloc/commit") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  bool is_empty;
  REQUIRE_OK(a0_transport_empty(lk, &is_empty));
  REQUIRE(is_empty);

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 144
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 144
    }
  },
  "data": [
  ]
}
)");

  a0_transport_frame_t first_frame;
  REQUIRE_OK(a0_transport_alloc(lk, 10, &first_frame));
  memcpy(first_frame.data, "0123456789", 10);
  REQUIRE_OK(a0_transport_commit(lk));

  a0_transport_frame_t second_frame;
  REQUIRE_OK(a0_transport_alloc(lk, 40, &second_frame));
  memcpy(second_frame.data, "0123456789012345678901234567890123456789", 40);

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 194
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208,
      "high_water_mark": 288
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 1,
      "prev_off": 0,
      "next_off": 208,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "committed": false,
      "off": 208,
      "seq": 2,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_commit(lk));

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208,
      "high_water_mark": 288
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208,
      "high_water_mark": 288
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 1,
      "prev_off": 0,
      "next_off": 208,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "off": 208,
      "seq": 2,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] evicts") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  bool evicts;

  REQUIRE_OK(a0_transport_alloc_evicts(lk, 2 * 1024, &evicts));
  REQUIRE(!evicts);

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_alloc(lk, 2 * 1024, &frame));

  REQUIRE_OK(a0_transport_alloc_evicts(lk, 2 * 1024, &evicts));
  REQUIRE(evicts);

  REQUIRE(a0_transport_alloc_evicts(lk, 4 * 1024, &evicts) == EOVERFLOW);

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] iteration") {
  // Create transport and close it.
  {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, arena));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_transport_frame_t first_frame;
    REQUIRE_OK(a0_transport_alloc(lk, 1, &first_frame));
    memcpy(first_frame.data, "A", 1);

    a0_transport_frame_t second_frame;
    REQUIRE_OK(a0_transport_alloc(lk, 2, &second_frame));
    memcpy(second_frame.data, "BB", 2);

    a0_transport_frame_t third_frame;
    REQUIRE_OK(a0_transport_alloc(lk, 3, &third_frame));
    memcpy(third_frame.data, "CCC", 3);

    REQUIRE_OK(a0_transport_commit(lk));

    REQUIRE_OK(a0_transport_unlock(lk));
  }

  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  bool is_empty;
  REQUIRE_OK(a0_transport_empty(lk, &is_empty));
  REQUIRE(!is_empty);

  REQUIRE_OK(a0_transport_jump_head(lk));

  a0_transport_frame_t frame;

  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  bool has_next;
  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(has_next);

  bool has_prev;
  REQUIRE_OK(a0_transport_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(!has_next);

  REQUIRE_OK(a0_transport_has_prev(lk, &has_prev));
  REQUIRE(has_prev);

  REQUIRE_OK(a0_transport_prev(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_transport_prev(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_transport_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_jump_tail(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE_OK(a0_transport_jump_head(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] empty jumps") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE(a0_transport_jump_head(lk) == EAGAIN);
  REQUIRE(a0_transport_jump_tail(lk) == EAGAIN);
  REQUIRE(a0_transport_next(lk) == EAGAIN);
  REQUIRE(a0_transport_prev(lk) == EAGAIN);

  bool has_next;
  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(!has_next);

  bool has_prev;
  REQUIRE_OK(a0_transport_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] wrap around") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  std::string data(1 * 1024, 'a');  // 1kB string
  for (int i = 0; i < 20; i++) {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
    memcpy(frame.data, data.c_str(), data.size());
  }

  REQUIRE_OK(a0_transport_commit(lk));

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2288,
      "off_tail": 1216,
      "high_water_mark": 3352
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2288,
      "off_tail": 1216,
      "high_water_mark": 3352
    }
  },
  "data": [
    {
      "off": 2288,
      "seq": 18,
      "prev_off": 1216,
      "next_off": 144,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 144,
      "seq": 19,
      "prev_off": 2288,
      "next_off": 1216,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 1216,
      "seq": 20,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] expired next") {
  a0_transport_t transport;
  a0_locked_transport_t lk;
  a0_transport_frame_t frame;
  std::string data(1 * 1024, 'a');  // 1kB string

  REQUIRE_OK(a0_transport_init(&transport, arena));
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());

  REQUIRE_OK(a0_transport_jump_head(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);

  REQUIRE_OK(a0_transport_unlock(lk));

  {
    a0_transport_t transport_other;
    REQUIRE_OK(a0_transport_init(&transport_other, arena));
    REQUIRE_OK(a0_transport_lock(&transport_other, &lk));

    for (int i = 0; i < 20; i++) {
      REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
      memcpy(frame.data, data.c_str(), data.size());
    }

    REQUIRE_OK(a0_transport_unlock(lk));
  }

  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  bool valid;
  REQUIRE_OK(a0_transport_ptr_valid(lk, &valid));
  REQUIRE(!valid);

  bool has_next;
  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 18);

  REQUIRE_OK(a0_transport_ptr_valid(lk, &valid));
  REQUIRE(valid);

  bool has_prev;
  REQUIRE_OK(a0_transport_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_has_next(lk, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] large alloc") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  std::string long_str(3 * 1024, 'a');  // 3kB string
  for (int i = 0; i < 5; i++) {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_alloc(lk, long_str.size(), &frame));
    memcpy(frame.data, long_str.c_str(), long_str.size());
    REQUIRE_OK(a0_transport_commit(lk));
  }

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 3256
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 3256
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 5,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 3072,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] resize") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  size_t used_space;
  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 144);

  std::string data(1024, 'a');
  a0_transport_frame_t frame;

  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 1208);

  REQUIRE(a0_transport_resize(lk, 0) == EINVAL);
  REQUIRE(a0_transport_resize(lk, 1207) == EINVAL);
  REQUIRE_OK(a0_transport_resize(lk, 1208));

  data = std::string(1024 + 1, 'a');  // 1 byte larger than previous.
  REQUIRE(a0_transport_alloc(lk, data.size(), &frame) == EOVERFLOW);

  data = std::string(1024, 'b');  // same size as existing.
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_jump_tail(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.data_size == 1024);
  REQUIRE(a0::test::str(frame) == data);
  REQUIRE(arena.buf.ptr[1207] == 'b');
  REQUIRE(arena.buf.ptr[1208] != 'b');

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 1208,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 1208
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 1208
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 2,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 1208);

  REQUIRE_OK(a0_transport_resize(lk, 4096));

  data = std::string(2 * 1024, 'c');
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 3304);

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 144,
      "off_tail": 1216,
      "high_water_mark": 3304
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 144,
      "off_tail": 1216,
      "high_water_mark": 3304
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 2,
      "prev_off": 0,
      "next_off": 1216,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    },
    {
      "off": 1216,
      "seq": 3,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 2048,
      "data": "ccccccccccccccccccccccccccccc..."
    }
  ]
}
)");

  // This forces an eviction of some existing data, reducing the high water mark.
  // We replace it with less data.
  data = std::string(16, 'd');
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 3368);

  data = std::string(3 * 1024, 'e');
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 3368);

  data = std::string(16, 'f');
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 3320);

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 144,
      "off_tail": 3264,
      "high_water_mark": 3320
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 144,
      "off_tail": 3264,
      "high_water_mark": 3320
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 5,
      "prev_off": 3312,
      "next_off": 3264,
      "data_size": 3072,
      "data": "eeeeeeeeeeeeeeeeeeeeeeeeeeeee..."
    },
    {
      "off": 3264,
      "seq": 6,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 16,
      "data": "ffffffffffffffff"
    }
  ]
}
)");

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with less data.
  data = std::string(3264, 'e');
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_commit(lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == (144 + 40 + 3264));

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with nothing.
  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE_OK(a0_transport_used_space(lk, &used_space));
  REQUIRE(used_space == 144);

  REQUIRE_OK(a0_transport_unlock(lk));
}

void fork_sleep_push(a0_transport_t* transport, const std::string& str) {
  if (!fork()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(transport, &lk));

    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_alloc(lk, str.size(), &frame));
    memcpy(frame.data, str.c_str(), str.size());
    REQUIRE_OK(a0_transport_commit(lk));

    REQUIRE_OK(a0_transport_shutdown(lk));
    REQUIRE_OK(a0_transport_unlock(lk));

    exit(0);
  }
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] disk await") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, disk.arena));

  fork_sleep_push(&transport, "ABC");

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE_OK(a0_transport_wait(lk, a0_transport_nonempty_pred(&lk)));

  REQUIRE_OK(a0_transport_jump_head(lk));

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_transport_wait(lk, a0_transport_nonempty_pred(&lk)));

  fork_sleep_push(&transport, "DEF");
  REQUIRE_OK(a0_transport_wait(lk, a0_transport_has_next_pred(&lk)));

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_transport_shutdown(lk));
  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] shm await") {
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, shm.arena));

  fork_sleep_push(&transport, "ABC");

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE_OK(a0_transport_wait(lk, a0_transport_nonempty_pred(&lk)));

  REQUIRE_OK(a0_transport_jump_head(lk));

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_transport_wait(lk, a0_transport_nonempty_pred(&lk)));

  fork_sleep_push(&transport, "DEF");
  REQUIRE_OK(a0_transport_wait(lk, a0_transport_has_next_pred(&lk)));

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_transport_shutdown(lk));
  REQUIRE_OK(a0_transport_unlock(lk));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust") {
  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, shm.arena));

    // Write one frame successfully.
    {
      a0_locked_transport_t lk;
      REQUIRE_OK(a0_transport_lock(&transport, &lk));

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_alloc(lk, 3, &frame));
      memcpy(frame.data, "YES", 3);
      REQUIRE_OK(a0_transport_commit(lk));

      REQUIRE_OK(a0_transport_unlock(lk));
    }

    // Write one frame unsuccessfully.
    {
      a0_locked_transport_t lk;
      REQUIRE_OK(a0_transport_lock(&transport, &lk));

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_alloc(lk, 2, &frame));
      memcpy(frame.data, "NO", 2);

      require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 187
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 192,
      "high_water_mark": 234
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 1,
      "prev_off": 0,
      "next_off": 192,
      "data_size": 3,
      "data": "YES"
    },
    {
      "committed": false,
      "off": 192,
      "seq": 2,
      "prev_off": 144,
      "next_off": 0,
      "data_size": 2,
      "data": "NO"
    }
  ]
}
)");

      // Exit without cleaning resources.
      std::quick_exit(0);
    }
  }
  int child_status;
  waitpid(child_pid, &child_status, 0);
  (void)child_status;

  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, shm.arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  require_debugstr(lk, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 187
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144,
      "high_water_mark": 187
    }
  },
  "data": [
    {
      "off": 144,
      "seq": 1,
      "prev_off": 0,
      "next_off": 192,
      "data_size": 3,
      "data": "YES"
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_shutdown(lk));
  REQUIRE_OK(a0_transport_unlock(lk));
}

std::string random_string(size_t length) {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust fuzz") {
  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, shm.arena));

    while (true) {
      a0_locked_transport_t lk;
      REQUIRE_OK(a0_transport_lock(&transport, &lk));

      auto str = random_string(rand() % 1024);

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_alloc(lk, str.size(), &frame));
      memcpy(frame.data, str.c_str(), str.size());
      REQUIRE_OK(a0_transport_commit(lk));

      REQUIRE_OK(a0_transport_unlock(lk));
    }
  }

  // Wait for child to run for a while, then violently kill it.
  if (a0::test::is_debug_mode()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  kill(child_pid, SIGKILL);
  int wstatus;
  REQUIRE(waitpid(child_pid, &wstatus, 0) == child_pid);

  // Connect to the transport.
  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, shm.arena));

  // Make sure the transport is still functinal.
  // We can still grab the lock, write, and read from the transport.
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));
  {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_alloc(lk, 11, &frame));
    memcpy(frame.data, "Still Works", 11);
    REQUIRE_OK(a0_transport_commit(lk));
  }
  REQUIRE_OK(a0_transport_jump_tail(lk));
  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(a0::test::str(frame) == "Still Works");

  REQUIRE_OK(a0_transport_shutdown(lk));
  REQUIRE_OK(a0_transport_unlock(lk));
}

void copy_file(std::string_view from, std::string_view to) {
  std::ifstream src(from.data(), std::ios::binary);
  std::ofstream dst(to.data(), std::ios::binary);

  dst << src.rdbuf();
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust copy shm->disk->shm") {
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);

  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, shm.arena));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_transport_frame_t frame;
    a0_transport_alloc(lk, str.size(), &frame);
    memcpy(frame.data, str.c_str(), str.size());
    a0_transport_commit(lk);

    // Do not unlock!
    // a0_transport_unlock(lk);

    // Exit without cleaning resources.
    std::quick_exit(0);
  }

  int child_status;
  waitpid(child_pid, &child_status, 0);
  (void)child_status;

  // Copy the shm file to disk.
  copy_file(TEST_SHM_ABS, COPY_DISK);

  // Copy the disk file to memory.
  copy_file(COPY_DISK, COPY_SHM_ABS);

  a0_file_t copied_shm;
  a0_file_open(COPY_SHM, &shmopt, &copied_shm);

  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, copied_shm.arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  a0_transport_jump_head(lk);
  a0_transport_frame_t frame;
  a0_transport_frame(lk, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_transport_unlock(lk));

  a0_file_remove(COPY_DISK);
  a0_file_close(&copied_shm);
  a0_file_remove(COPY_SHM);
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust copy disk->shm->disk") {
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);

  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, disk.arena));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_transport_frame_t frame;
    a0_transport_alloc(lk, str.size(), &frame);
    memcpy(frame.data, str.c_str(), str.size());
    a0_transport_commit(lk);

    // Do not unlock!
    // a0_transport_unlock(lk);

    // Exit without cleaning resources.
    std::quick_exit(0);
  }

  int child_status;
  waitpid(child_pid, &child_status, 0);
  (void)child_status;

  // Copy the shm file to disk.
  copy_file(TEST_DISK, COPY_SHM_ABS);

  // Copy the disk file to memory.
  copy_file(COPY_SHM_ABS, COPY_DISK);

  a0_file_t copied_disk;
  a0_file_open(COPY_DISK, &diskopt, &copied_disk);

  a0_transport_t transport;
  REQUIRE_OK(a0_transport_init(&transport, copied_disk.arena));

  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  a0_transport_jump_head(lk);
  a0_transport_frame_t frame;
  a0_transport_frame(lk, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_transport_unlock(lk));

  a0_file_close(&copied_disk);
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);
}
