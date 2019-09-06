#include <a0/stream.h>

#include <doctest.h>
#include <string.h>
#include <unistd.h>

#include "src/stream_debug.h"
#include "src/test_util.hh"

static const char kTestShm[] = "/test.shm";
static const char kProtocolName[] = "my_protocol";

struct StreamTestFixture {
  StreamTestFixture() {
    a0_shmobj_unlink(kTestShm);
    shmopt.size = 4096;
    a0_shmobj_open(kTestShm, &shmopt, &shmobj);

    protocol.name.ptr = (uint8_t*)kProtocolName;
    protocol.name.size = strlen(kProtocolName);
    protocol.major_version = 1;
    protocol.minor_version = 2;
    protocol.patch_version = 3;
    protocol.metadata_size = 17;
  }
  ~StreamTestFixture() {
    a0_shmobj_close(&shmobj);
    a0_shmobj_unlink(kTestShm);
  }

  void require_debugstr(a0_locked_stream_t lk, const std::string& expected) {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(a0::test::str(debugstr) == expected);
    free(debugstr.ptr);
  }

  a0_shmobj_options_t shmopt;
  a0_shmobj_t shmobj;
  a0_stream_protocol_t protocol;
};

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream construct") {
  REQUIRE(*(uint64_t*)shmobj.ptr != 0x616c65667a65726f);

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  REQUIRE(a0_unlock_stream(lk) == A0_OK);

  REQUIRE(*(uint64_t*)shmobj.ptr == 0x616c65667a65726f);
  REQUIRE(init_status == A0_STREAM_CREATED);

  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);

  a0_buf_t protocol_metadata;
  protocol.patch_version++;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
  REQUIRE(protocol_metadata.size == 17);
  REQUIRE((uintptr_t)protocol_metadata.ptr % alignof(max_align_t) == 0);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);
  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MISMATCH);
  protocol.patch_version--;

  REQUIRE(a0_lock_stream(&stream, &lk) == A0_OK);

  a0_stream_protocol_t read_protocol;
  REQUIRE(a0_stream_protocol(lk, &read_protocol, nullptr) == A0_OK);
  REQUIRE(a0::test::str(read_protocol.name) == kProtocolName);
  REQUIRE(read_protocol.major_version == 1);
  REQUIRE(read_protocol.minor_version == 2);
  REQUIRE(read_protocol.patch_version == 3);
  REQUIRE(read_protocol.metadata_size == 17);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream alloc/commit") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  a0_buf_t protocol_metadata;
  REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  bool is_empty;
  REQUIRE(a0_stream_empty(lk, &is_empty) == A0_OK);
  REQUIRE(is_empty);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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
  REQUIRE(a0_stream_alloc(lk, 10, &first_frame) == A0_OK);
  memcpy(first_frame.data, "0123456789", 10);
  REQUIRE(a0_stream_commit(lk) == A0_OK);

  a0_stream_frame_t second_frame;
  REQUIRE(a0_stream_alloc(lk, 40, &second_frame) == A0_OK);
  memcpy(second_frame.data, "0123456789012345678901234567890123456789", 40);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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
      "next_off": 272,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "committed": false,
      "off": 272,
      "seq": 2,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE(a0_stream_commit(lk) == A0_OK);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 224,
      "off_tail": 272
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
      "next_off": 272,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "off": 272,
      "seq": 2,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream iteration") {
  // Create stream and close it.
  {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
    a0_buf_t protocol_metadata;
    REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
    memcpy(protocol_metadata.ptr, "protocol metadata", 17);

    a0_stream_frame_t first_frame;
    REQUIRE(a0_stream_alloc(lk, 1, &first_frame) == A0_OK);
    memcpy(first_frame.data, "A", 1);

    a0_stream_frame_t second_frame;
    REQUIRE(a0_stream_alloc(lk, 2, &second_frame) == A0_OK);
    memcpy(second_frame.data, "BB", 2);

    a0_stream_frame_t third_frame;
    REQUIRE(a0_stream_alloc(lk, 3, &third_frame) == A0_OK);
    memcpy(third_frame.data, "CCC", 3);

    REQUIRE(a0_stream_commit(lk) == A0_OK);

    REQUIRE(a0_unlock_stream(lk) == A0_OK);
    REQUIRE(a0_stream_close(&stream) == A0_OK);
  }

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);

  bool is_empty;
  REQUIRE(a0_stream_empty(lk, &is_empty) == A0_OK);
  REQUIRE(!is_empty);

  REQUIRE(a0_stream_jump_head(lk) == A0_OK);

  a0_stream_frame_t frame;

  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  bool has_next;
  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(!has_next);

  REQUIRE(a0_stream_jump_head(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE(a0_stream_jump_tail(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream wrap around") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  a0_buf_t protocol_metadata;
  REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  std::string data(1 * 1024, 'a');  // 1kB string
  for (int i = 0; i < 20; i++) {
    a0_stream_frame_t first_frame;
    REQUIRE(a0_stream_alloc(lk, data.size(), &first_frame) == A0_OK);
    memcpy(first_frame.data, data.c_str(), data.size());
  }

  REQUIRE(a0_stream_commit(lk) == A0_OK);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2336,
      "off_tail": 1280
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2336,
      "off_tail": 1280
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
      "off": 2336,
      "seq": 18,
      "next_off": 224,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 224,
      "seq": 19,
      "next_off": 1280,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 1280,
      "seq": 20,
      "next_off": 2336,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream large alloc") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  a0_buf_t protocol_metadata;
  REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);

  std::string long_str(3 * 1024, 'a');  // 3kB string
  for (int i = 0; i < 5; i++) {
    a0_stream_frame_t first_frame;
    REQUIRE(a0_stream_alloc(lk, long_str.size(), &first_frame) == A0_OK);
    memcpy(first_frame.data, long_str.c_str(), long_str.size());
    REQUIRE(a0_stream_commit(lk) == A0_OK);
  }

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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
      "next_off": 0,
      "data_size": 3072,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

void fork_sleep_push(a0_stream_t* stream, const std::string& str) {
  if (!fork()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    a0_locked_stream_t lk;
    REQUIRE(a0_lock_stream(stream, &lk) == A0_OK);

    a0_stream_frame_t frame;
    REQUIRE(a0_stream_alloc(lk, str.size(), &frame) == A0_OK);
    memcpy(frame.data, str.c_str(), str.size());
    REQUIRE(a0_stream_commit(lk) == A0_OK);

    REQUIRE(a0_unlock_stream(lk) == A0_OK);
    REQUIRE(a0_stream_close(stream) == A0_OK);

    exit(0);
  }
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream await") {
  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  a0_buf_t protocol_metadata;
  REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
  memcpy(protocol_metadata.ptr, "protocol metadata", 17);
  REQUIRE(a0_unlock_stream(lk) == A0_OK);

  fork_sleep_push(&stream, "ABC");

  REQUIRE(a0_lock_stream(&stream, &lk) == A0_OK);

  REQUIRE(a0_stream_await(lk, a0_stream_nonempty) == A0_OK);

  REQUIRE(a0_stream_jump_head(lk) == A0_OK);

  a0_stream_frame_t frame;
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE(a0_stream_await(lk, a0_stream_nonempty) == A0_OK);

  fork_sleep_push(&stream, "DEF");
  REQUIRE(a0_stream_await(lk, a0_stream_has_next) == A0_OK);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_FIXTURE(StreamTestFixture, "Test stream robust") {
  if (!fork()) {
    a0_stream_t stream;
    a0_stream_init_status_t init_status;
    a0_locked_stream_t lk;
    REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
    REQUIRE(init_status == A0_STREAM_CREATED);
    a0_buf_t protocol_metadata;
    REQUIRE(a0_stream_protocol(lk, nullptr, &protocol_metadata) == A0_OK);
    memcpy(protocol_metadata.ptr, "protocol metadata", 17);
    REQUIRE(a0_unlock_stream(lk) == A0_OK);

    // Write one frame successfully.
    {
      a0_locked_stream_t lk;
      REQUIRE(a0_lock_stream(&stream, &lk) == A0_OK);

      a0_stream_frame_t frame;
      REQUIRE(a0_stream_alloc(lk, 3, &frame) == A0_OK);
      memcpy(frame.data, "YES", 3);
      REQUIRE(a0_stream_commit(lk) == A0_OK);

      REQUIRE(a0_unlock_stream(lk) == A0_OK);
    }


    // Write one frame unsuccessfully.
    {
      a0_locked_stream_t lk;
      REQUIRE(a0_lock_stream(&stream, &lk) == A0_OK);

      a0_stream_frame_t frame;
      REQUIRE(a0_stream_alloc(lk, 2, &frame) == A0_OK);
      memcpy(frame.data, "NO", 2);

      require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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
      "next_off": 272,
      "data_size": 3,
      "data": "YES"
    },
    {
      "committed": false,
      "off": 272,
      "seq": 2,
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
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  a0_stream_t stream;
  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  REQUIRE(a0_stream_init(&stream, shmobj, protocol, &init_status, &lk) == A0_OK);
  REQUIRE(init_status == A0_STREAM_PROTOCOL_MATCH);

  require_debugstr(lk, R"(
{
  "header": {
    "shmobj_size": 4096,
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
      "next_off": 272,
      "data_size": 3,
      "data": "YES"
    }
  ]
}
)");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}
