#include <a0/shm.h>
#include <a0/transport.h>

#include <doctest.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>

#include "src/strutil.hpp"
#include "src/test_util.hpp"
#include "src/transport_debug.h"

static const char TEST_SHM[] = "/test.shm";

struct StreamTestFixture {
  StreamTestFixture() {
    a0_shm_unlink(TEST_SHM);
    shmopt = {
        .size = 4096,
        .resize = false,
    };
    a0_shm_open(TEST_SHM, &shmopt, &shm);
  }
  ~StreamTestFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(TEST_SHM);
  }

  void require_debugstr(a0_locked_transport_t lk, const std::string& expected) {
    a0_buf_t debugstr;
    a0_transport_debugstr(lk, &debugstr);
    REQUIRE(a0::test::str(debugstr) == expected);
    free(debugstr.ptr);
  }

  a0_shm_options_t shmopt;
  a0_shm_t shm;
};

TEST_CASE_FIXTURE(StreamTestFixture, "transport] construct") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, 0, &init_status, &lk));
  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE(init_status == A0_TRANSPORT_CREATED);
  REQUIRE_OK(a0_transport_close(&transport));

  REQUIRE_OK(a0_transport_init(&transport, shm.buf, 0, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CONNECTED);

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    }
  },
  "metadata": "",
  "data": [
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] metadata") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;

  a0_buf_t arena = {
      .ptr = (uint8_t*)malloc(1024),
      .size = 1024,
  };
  memset(arena.ptr, 0, arena.size);

  a0_buf_t metadata_orig = {
      .ptr = (uint8_t*)"Hello, foo!",
      .size = 11,
  };
  REQUIRE_OK(a0_transport_init(&transport, arena, metadata_orig.size, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CREATED);

  a0_buf_t got_metadata;
  REQUIRE_OK(a0_transport_metadata(lk, &got_metadata));
  REQUIRE(got_metadata.size == metadata_orig.size);
  memcpy(got_metadata.ptr, metadata_orig.ptr, metadata_orig.size);

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));

  REQUIRE_OK(a0_transport_init(&transport, arena, 99, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CONNECTED);
  REQUIRE_OK(a0_transport_metadata(lk, &got_metadata));
  REQUIRE(got_metadata.size == metadata_orig.size);
  REQUIRE(!memcmp(got_metadata.ptr, metadata_orig.ptr, metadata_orig.size));

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 1024,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    }
  },
  "metadata": "Hello, foo!",
  "data": [
  ]
}
)");

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));

  free(arena.ptr);
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] metadata too large") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;

  a0_buf_t arena = {
      .ptr = (uint8_t*)malloc(1024),
      .size = 1024,
  };
  memset(arena.ptr, 0, arena.size);
  REQUIRE(a0_transport_init(&transport, arena, 1024, &init_status, &lk) == ENOMEM);
  free(arena.ptr);
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] alloc/commit") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

  bool is_empty;
  REQUIRE_OK(a0_transport_empty(lk, &is_empty));
  REQUIRE(is_empty);

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0
    }
  },
  "metadata": "",
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
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208
    }
  },
  "metadata": "",
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
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 208
    }
  },
  "metadata": "",
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
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] alloc/commit") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

  bool evicts;

  REQUIRE_OK(a0_transport_alloc_evicts(lk, 2 * 1024, &evicts));
  REQUIRE(!evicts);

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_alloc(lk, 2 * 1024, &frame));

  REQUIRE_OK(a0_transport_alloc_evicts(lk, 2 * 1024, &evicts));
  REQUIRE(evicts);

  REQUIRE(a0_transport_alloc_evicts(lk, 4 * 1024, &evicts) == EOVERFLOW);

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] iteration") {
  // Create transport and close it.
  {
    a0_transport_t transport;
    a0_transport_init_status_t init_status;
    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

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
    REQUIRE_OK(a0_transport_close(&transport));
  }

  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

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
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] empty jumps") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

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
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] wrap around") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

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
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2288,
      "off_tail": 1216
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2288,
      "off_tail": 1216
    }
  },
  "metadata": "",
  "data": [
    {
      "off": 2288,
      "seq": 18,
      "prev_off": 0,
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
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] expired next") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  a0_transport_frame_t frame;
  std::string data(1 * 1024, 'a');  // 1kB string

  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

  REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());

  REQUIRE_OK(a0_transport_jump_head(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);

  REQUIRE_OK(a0_transport_unlock(lk));

  {
    a0_transport_t transport_other;
    REQUIRE_OK(a0_transport_init(&transport_other, shm.buf, A0_NONE, &init_status, &lk));

    for (int i = 0; i < 20; i++) {
      REQUIRE_OK(a0_transport_alloc(lk, data.size(), &frame));
      memcpy(frame.data, data.c_str(), data.size());
    }

    REQUIRE_OK(a0_transport_unlock(lk));
    REQUIRE_OK(a0_transport_close(&transport_other));
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
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] large alloc") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));

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
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 144,
      "off_tail": 144
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 144,
      "off_tail": 144
    }
  },
  "metadata": "",
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
  REQUIRE_OK(a0_transport_close(&transport));
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

    REQUIRE_OK(a0_transport_unlock(lk));
    REQUIRE_OK(a0_transport_close(transport));

    exit(0);
  }
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] await") {
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
  REQUIRE_OK(a0_transport_unlock(lk));

  fork_sleep_push(&transport, "ABC");

  REQUIRE_OK(a0_transport_lock(&transport, &lk));

  REQUIRE_OK(a0_transport_await(lk, a0_transport_nonempty));

  REQUIRE_OK(a0_transport_jump_head(lk));

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_transport_await(lk, a0_transport_nonempty));

  fork_sleep_push(&transport, "DEF");
  REQUIRE_OK(a0_transport_await(lk, a0_transport_has_next));

  REQUIRE_OK(a0_transport_next(lk));
  REQUIRE_OK(a0_transport_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));
}

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust") {
  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    a0_transport_init_status_t init_status;
    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
    REQUIRE(init_status == A0_TRANSPORT_CREATED);
    REQUIRE_OK(a0_transport_unlock(lk));

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
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 144,
      "off_tail": 192
    }
  },
  "metadata": "",
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
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CONNECTED);

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 144,
      "off_tail": 144
    }
  },
  "metadata": "",
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

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));
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
    a0_transport_init_status_t init_status;
    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
    REQUIRE(init_status == A0_TRANSPORT_CREATED);
    REQUIRE_OK(a0_transport_unlock(lk));

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
  if (a0::test::is_valgrind()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  kill(child_pid, SIGKILL);
  int wstatus;
  REQUIRE(waitpid(child_pid, &wstatus, 0) == child_pid);

  // Connect to the transport.
  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CONNECTED);
  REQUIRE_OK(a0_transport_unlock(lk));

  // Make sure the transport is still functinal.
  // We can still grab the lock, write, and read from the transport.
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

  REQUIRE_OK(a0_transport_unlock(lk));
  REQUIRE_OK(a0_transport_close(&transport));
}

static const char COPY_SHM[] = "/copy.shm";

TEST_CASE_FIXTURE(StreamTestFixture, "transport] robust copy") {
  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_transport_t transport;
    a0_transport_init_status_t init_status;
    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_init(&transport, shm.buf, A0_NONE, &init_status, &lk));
    REQUIRE(init_status == A0_TRANSPORT_CREATED);
    REQUIRE_OK(a0_transport_unlock(lk));

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
  {
    auto cmd = a0::strutil::fmt("cp /dev/shm%s /tmp%s", TEST_SHM, COPY_SHM);
    REQUIRE_OK(system(cmd.c_str()));
  }

  // Copy the disk file to memory.
  {
    auto cmd = a0::strutil::fmt("cp /tmp%s /dev/shm%s", COPY_SHM, COPY_SHM);
    REQUIRE_OK(system(cmd.c_str()));
  }

  a0_shm_t copied_shm;
  a0_shm_open(COPY_SHM, &shmopt, &copied_shm);

  a0_transport_t transport;
  a0_transport_init_status_t init_status;
  a0_locked_transport_t lk;
  REQUIRE_OK(a0_transport_init(&transport, copied_shm.buf, A0_NONE, &init_status, &lk));
  REQUIRE(init_status == A0_TRANSPORT_CONNECTED);

  a0_transport_jump_head(lk);
  a0_transport_frame_t frame;
  a0_transport_frame(lk, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_transport_unlock(lk));

  a0_shm_close(&copied_shm);
  a0_shm_unlink(COPY_SHM);

  // Remove the disk file.
  {
    auto cmd = a0::strutil::fmt("rm /tmp%s", COPY_SHM);
    REQUIRE_OK(system(cmd.c_str()));
  }
}
