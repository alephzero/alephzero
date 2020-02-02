#include <a0/shm.h>
#include <a0/stream.h>

#include <doctest.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/stream_debug.h"
#include "src/strutil.hpp"
#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";
static const char kProtocolName[] = "my_protocol";

struct StreamTestFixture {
  StreamTestFixture() {
    a0_shm_unlink(kTestShm);
    shmopt.size = 4096;
    a0_shm_open(kTestShm, &shmopt, &shm);

    protocol.name.ptr = (uint8_t*)kProtocolName;
    protocol.name.size = sizeof(kProtocolName);
    protocol.major_version = 1;
    protocol.minor_version = 2;
    protocol.patch_version = 3;
    protocol.metadata_size = 17;
  }
  ~StreamTestFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(kTestShm);
  }

  void require_debugstr(a0_locked_stream_t lk, const std::string& expected) {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(a0::test::str(debugstr) == expected);
    free(debugstr.ptr);
  }

  a0_shm_options_t shmopt;
  a0_shm_t shm;
  a0_stream_protocol_t protocol;
};

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream construct") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE(init_status == A0_STREAM_CREATED);
  REQUIRE_OK(a0_stream_close(&stream));

  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);

  a0_buf_t protocol_metadata;
  protocol.patch_version++;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
  REQUIRE(protocol_metadata.size == 17);
  REQUIRE((uintptr_t)protocol_metadata.ptr % alignof(max_align_t) == 0);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);
  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MISMATCH);
  protocol.patch_version--;

  REQUIRE_OK(a0_lock_stream(&stream, &lk));

  a0_stream_protocol_t read_protocol;
  REQUIRE_OK(a0_stream_protocol(lk, &read_protocol, nullptr));
  REQUIRE(memcmp(read_protocol.name.ptr, kProtocolName, read_protocol.name.size) == 0);
  REQUIRE(read_protocol.major_version == 1);
  REQUIRE(read_protocol.minor_version == 2);
  REQUIRE(read_protocol.patch_version == 3);
  REQUIRE(read_protocol.metadata_size == 17);

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
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
  ]
}
)");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test metadata too large") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;

  a0_buf_t arena = {
      .ptr = (uint8_t*)malloc(1024),
      .size = 1024,
  };
  memset(arena.ptr, 0, arena.size);
  protocol.metadata_size = 1024;
  REQUIRE(a0_stream_init(&stream, arena, protocol, &init_status, &lk) == ENOMEM);
  free(arena.ptr);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream alloc/commit") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  a0_buf_t protocol_metadata;
  REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  bool is_empty;
  REQUIRE_OK(a0_stream_empty(lk, &is_empty));
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
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
  ]
}
)");

  a0_stream_frame_t first_frame;
  REQUIRE_OK(a0_stream_alloc(lk, 10, &first_frame));
  memcpy(first_frame.data, "0123456789", 10);
  REQUIRE_OK(a0_stream_commit(lk));

  a0_stream_frame_t second_frame;
  REQUIRE_OK(a0_stream_alloc(lk, 40, &second_frame));
  memcpy(second_frame.data, "0123456789012345678901234567890123456789", 40);

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 224,
      "off_tail": 224
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 224,
      "off_tail": 288
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 224,
      "seq": 1,
      "prev_off": 0,
      "next_off": 288,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "committed": false,
      "off": 288,
      "seq": 2,
      "prev_off": 224,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_stream_commit(lk));

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 224,
      "off_tail": 288
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 224,
      "off_tail": 288
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 224,
      "seq": 1,
      "prev_off": 0,
      "next_off": 288,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "off": 288,
      "seq": 2,
      "prev_off": 224,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream iteration") {
  // Create stream and close it.
  {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
    a0_buf_t protocol_metadata;
    REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
    memcpy(protocol_metadata.ptr, "protocol metadata", 17);

    a0_stream_frame_t first_frame;
    REQUIRE_OK(a0_stream_alloc(lk, 1, &first_frame));
    memcpy(first_frame.data, "A", 1);

    a0_stream_frame_t second_frame;
    REQUIRE_OK(a0_stream_alloc(lk, 2, &second_frame));
    memcpy(second_frame.data, "BB", 2);

    a0_stream_frame_t third_frame;
    REQUIRE_OK(a0_stream_alloc(lk, 3, &third_frame));
    memcpy(third_frame.data, "CCC", 3);

    REQUIRE_OK(a0_stream_commit(lk));

    REQUIRE_OK(a0_unlock_stream(lk));
    REQUIRE_OK(a0_stream_close(&stream));
  }

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));

  bool is_empty;
  REQUIRE_OK(a0_stream_empty(lk, &is_empty));
  REQUIRE(!is_empty);

  REQUIRE_OK(a0_stream_jump_head(lk));

  a0_stream_frame_t frame;

  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  bool has_next;
  REQUIRE_OK(a0_stream_has_next(lk, &has_next));
  REQUIRE(has_next);

  bool has_prev;
  REQUIRE_OK(a0_stream_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_stream_next(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_stream_has_next(lk, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_stream_next(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE_OK(a0_stream_has_next(lk, &has_next));
  REQUIRE(!has_next);

  REQUIRE_OK(a0_stream_has_prev(lk, &has_prev));
  REQUIRE(has_prev);

  REQUIRE_OK(a0_stream_prev(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_stream_prev(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_stream_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_stream_jump_tail(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE_OK(a0_stream_jump_head(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test empty jumps") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));

  REQUIRE(a0_stream_jump_head(lk) == EAGAIN);
  REQUIRE(a0_stream_jump_tail(lk) == EAGAIN);
  REQUIRE(a0_stream_next(lk) == EAGAIN);
  REQUIRE(a0_stream_prev(lk) == EAGAIN);

  bool has_next;
  REQUIRE_OK(a0_stream_has_next(lk, &has_next));
  REQUIRE(!has_next);

  bool has_prev;
  REQUIRE_OK(a0_stream_has_prev(lk, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream wrap around") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  a0_buf_t protocol_metadata;
  REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  std::string data(1 * 1024, 'a');  // 1kB string
  for (int i = 0; i < 20; i++) {
    a0_stream_frame_t first_frame;
    REQUIRE_OK(a0_stream_alloc(lk, data.size(), &first_frame));
    memcpy(first_frame.data, data.c_str(), data.size());
  }

  REQUIRE_OK(a0_stream_commit(lk));

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2368,
      "off_tail": 1296
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2368,
      "off_tail": 1296
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 2368,
      "seq": 18,
      "prev_off": 0,
      "next_off": 224,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 224,
      "seq": 19,
      "prev_off": 2368,
      "next_off": 1296,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 1296,
      "seq": 20,
      "prev_off": 224,
      "next_off": 0,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream large alloc") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  a0_buf_t protocol_metadata;
  REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  std::string long_str(3 * 1024, 'a');  // 3kB string
  for (int i = 0; i < 5; i++) {
    a0_stream_frame_t first_frame;
    REQUIRE_OK(a0_stream_alloc(lk, long_str.size(), &first_frame));
    memcpy(first_frame.data, long_str.c_str(), long_str.size());
    REQUIRE_OK(a0_stream_commit(lk));
  }

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 224,
      "off_tail": 224
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 224,
      "off_tail": 224
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 224,
      "seq": 5,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 3072,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

void fork_sleep_push(a0_stream_t* stream, const std::string& str) {
  if (!fork()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    a0_locked_stream_t lk;
    REQUIRE_OK(a0_lock_stream(stream, &lk));

    a0_stream_frame_t frame;
    REQUIRE_OK(a0_stream_alloc(lk, str.size(), &frame));
    memcpy(frame.data, str.c_str(), str.size());
    REQUIRE_OK(a0_stream_commit(lk));

    REQUIRE_OK(a0_unlock_stream(lk));
    REQUIRE_OK(a0_stream_close(stream));

    exit(0);
  }
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream await") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  a0_buf_t protocol_metadata;
  REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);
  REQUIRE_OK(a0_unlock_stream(lk));

  fork_sleep_push(&stream, "ABC");

  REQUIRE_OK(a0_lock_stream(&stream, &lk));

  REQUIRE_OK(a0_stream_await(lk, a0_stream_nonempty));

  REQUIRE_OK(a0_stream_jump_head(lk));

  a0_stream_frame_t frame;
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_stream_await(lk, a0_stream_nonempty));

  fork_sleep_push(&stream, "DEF");
  REQUIRE_OK(a0_stream_await(lk, a0_stream_has_next));

  REQUIRE_OK(a0_stream_next(lk));
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream robust") {
  int child_pid = fork();
  if (!child_pid) {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
    REQUIRE(init_status == A0_STREAM_CREATED);
    a0_buf_t protocol_metadata;
    REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
    memcpy(protocol_metadata.ptr, "protocol metadata", 17);
    REQUIRE_OK(a0_unlock_stream(lk));

    // Write one frame successfully.
    {
      a0_locked_stream_t lk;
      REQUIRE_OK(a0_lock_stream(&stream, &lk));

      a0_stream_frame_t frame;
      REQUIRE_OK(a0_stream_alloc(lk, 3, &frame));
      memcpy(frame.data, "YES", 3);
      REQUIRE_OK(a0_stream_commit(lk));

      REQUIRE_OK(a0_unlock_stream(lk));
    }

    // Write one frame unsuccessfully.
    {
      a0_locked_stream_t lk;
      REQUIRE_OK(a0_lock_stream(&stream, &lk));

      a0_stream_frame_t frame;
      REQUIRE_OK(a0_stream_alloc(lk, 2, &frame));
      memcpy(frame.data, "NO", 2);

      require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 224,
      "off_tail": 224
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 224,
      "off_tail": 272
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 224,
      "seq": 1,
      "prev_off": 0,
      "next_off": 272,
      "data_size": 3,
      "data": "YES"
    },
    {
      "committed": false,
      "off": 272,
      "seq": 2,
      "prev_off": 224,
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
  int unused_status;
  waitpid(child_pid, &unused_status, 0);
  (void)unused_status;

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);

  require_debugstr(lk, R"(
{
  "header": {
    "shm_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 224,
      "off_tail": 224
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 224,
      "off_tail": 224
    }
  },
  "protocol": {
    "name": "my_protocol",
    "semver": "1.2.3",
    "metadata_size": 17,
    "metadata": "protocol metadata"
  },
  "data": [
    {
      "off": 224,
      "seq": 1,
      "prev_off": 0,
      "next_off": 272,
      "data_size": 3,
      "data": "YES"
    }
  ]
}
)");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
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

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream robust fuzz") {
  int child_pid = fork();
  if (!child_pid) {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
    REQUIRE(init_status == A0_STREAM_CREATED);
    a0_buf_t protocol_metadata;
    REQUIRE_OK(a0_stream_protocol(lk, nullptr, &protocol_metadata));
    memcpy(protocol_metadata.ptr, "protocol metadata", 17);
    REQUIRE_OK(a0_unlock_stream(lk));

    while (true) {
      a0_locked_stream_t lk;
      REQUIRE_OK(a0_lock_stream(&stream, &lk));

      auto str = random_string(rand() % 1024);

      a0_stream_frame_t frame;
      REQUIRE_OK(a0_stream_alloc(lk, str.size(), &frame));
      memcpy(frame.data, str.c_str(), str.size());
      REQUIRE_OK(a0_stream_commit(lk));

      REQUIRE_OK(a0_unlock_stream(lk));
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

  // Connect to the stream.
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);
  REQUIRE_OK(a0_unlock_stream(lk));

  // Make sure the stream is still functinal.
  // We can still grab the lock, write, and read from the stream.
  REQUIRE_OK(a0_lock_stream(&stream, &lk));
  {
    a0_stream_frame_t frame;
    REQUIRE_OK(a0_stream_alloc(lk, 11, &frame));
    memcpy(frame.data, "Still Works", 11);
    REQUIRE_OK(a0_stream_commit(lk));
  }
  REQUIRE_OK(a0_stream_jump_tail(lk));
  a0_stream_frame_t frame;
  REQUIRE_OK(a0_stream_frame(lk, &frame));
  REQUIRE(a0::test::str(frame) == "Still Works");

  REQUIRE_OK(a0_unlock_stream(lk));
  REQUIRE_OK(a0_stream_close(&stream));
}

static const char kCopyShm[] = "/copy.shm";

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream robust copy") {
  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE_OK(a0_stream_init(&stream, shm.buf, protocol, &init_status, &lk));
    REQUIRE(init_status == A0_STREAM_CREATED);
    REQUIRE_OK(a0_unlock_stream(lk));

    a0_stream_frame_t frame;
    a0_stream_alloc(lk, str.size(), &frame);
    memcpy(frame.data, str.c_str(), str.size());
    a0_stream_commit(lk);

    // Do not unlock!
    // a0_unlock_stream(lk);

    // Exit without cleaning resources.
    std::quick_exit(0);
  }

  int unused_status;
  waitpid(child_pid, &unused_status, 0);
  (void)unused_status;

  // Copy the shm file to disk.
  {
    auto cmd = a0::strutil::fmt("cp /dev/shm%s /tmp%s", kTestShm, kCopyShm);
    REQUIRE_OK(system(cmd.c_str()));
  }

  // Copy the disk file to memory.
  {
    auto cmd = a0::strutil::fmt("cp /tmp%s /dev/shm%s", kCopyShm, kCopyShm);
    REQUIRE_OK(system(cmd.c_str()));
  }

  a0_shm_t copied_shm;
  a0_shm_open(kCopyShm, &shmopt, &copied_shm);

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE_OK(a0_stream_init(&stream, copied_shm.buf, protocol, &init_status, &lk));
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);

  a0_stream_jump_head(lk);
  a0_stream_frame_t frame;
  a0_stream_frame(lk, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_unlock_stream(lk));

  a0_shm_close(&copied_shm);
  a0_shm_unlink(kCopyShm);

  // Remove the disk file.
  {
    auto cmd = a0::strutil::fmt("rm /tmp%s", kCopyShm);
    REQUIRE_OK(system(cmd.c_str()));
  }
}
