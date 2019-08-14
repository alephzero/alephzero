#include <a0/stream.h>

#include <a0/internal/stream_debug.h>
#include <a0/internal/test_util.hh>

#include <doctest.h>
#include <string.h>
#include <unistd.h>

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
  REQUIRE(str(read_protocol.name) == kProtocolName);
  REQUIRE(read_protocol.major_version == 1);
  REQUIRE(read_protocol.minor_version == 2);
  REQUIRE(read_protocol.patch_version == 3);
  REQUIRE(read_protocol.metadata_size == 17);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [0, 0]
-- head @ = 0
-- tail @ = 0
-------------------------
Working state
-- seq    = [0, 0]
-- head @ = 0
-- tail @ = 0
=========================
PROTOCOL INFO
-------------------------
-- name          = 'my_protocol'
-- semver        = 1.2.3
-- metadata size = 17
-- metadata      = 'protocol metadata'
=========================
DATA
=========================
)");
    free(debugstr.ptr);
  }

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

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [0, 0]
-- head @ = 0
-- tail @ = 0
-------------------------
Working state
-- seq    = [0, 0]
-- head @ = 0
-- tail @ = 0
=========================
PROTOCOL INFO
-------------------------
-- name          = 'my_protocol'
-- semver        = 1.2.3
-- metadata size = 17
-- metadata      = 'protocol metadata'
=========================
DATA
=========================
)");
    free(debugstr.ptr);
  }

  a0_stream_frame_t first_frame;
  REQUIRE(a0_stream_alloc(lk, 10, &first_frame) == A0_OK);
  memcpy(first_frame.data.ptr, "0123456789", 10);
  REQUIRE(a0_stream_commit(lk) == A0_OK);

  a0_stream_frame_t second_frame;
  REQUIRE(a0_stream_alloc(lk, 40, &second_frame) == A0_OK);
  memcpy(second_frame.data.ptr, "0123456789012345678901234567890123456789", 40);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [1, 1]
-- head @ = 224
-- tail @ = 224
-------------------------
Working state
-- seq    = [1, 2]
-- head @ = 224
-- tail @ = 272
=========================
PROTOCOL INFO
-------------------------
-- name          = 'my_protocol'
-- semver        = 1.2.3
-- metadata size = 17
-- metadata      = 'protocol metadata'
=========================
DATA
-------------------------
Frame
-- @      = 224
-- seq    = 1
-- next @ = 272
-- size   = 10
-- data   = '0123456789'
-------------------------
Frame (not committed)
-- @      = 272
-- seq    = 2
-- next @ = 0
-- size   = 40
-- data   = '01234567890123456789012345678...'
=========================
)");
    free(debugstr.ptr);
  }

  REQUIRE(a0_stream_commit(lk) == A0_OK);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(lk, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [1, 2]
-- head @ = 224
-- tail @ = 272
-------------------------
Working state
-- seq    = [1, 2]
-- head @ = 224
-- tail @ = 272
=========================
PROTOCOL INFO
-------------------------
-- name          = 'my_protocol'
-- semver        = 1.2.3
-- metadata size = 17
-- metadata      = 'protocol metadata'
=========================
DATA
-------------------------
Frame
-- @      = 224
-- seq    = 1
-- next @ = 272
-- size   = 10
-- data   = '0123456789'
-------------------------
Frame
-- @      = 272
-- seq    = 2
-- next @ = 0
-- size   = 40
-- data   = '01234567890123456789012345678...'
=========================
)");
    free(debugstr.ptr);
  }

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
    memcpy(first_frame.data.ptr, "A", 1);

    a0_stream_frame_t second_frame;
    REQUIRE(a0_stream_alloc(lk, 2, &second_frame) == A0_OK);
    memcpy(second_frame.data.ptr, "BB", 2);

    a0_stream_frame_t third_frame;
    REQUIRE(a0_stream_alloc(lk, 3, &third_frame) == A0_OK);
    memcpy(third_frame.data.ptr, "CCC", 3);

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
  REQUIRE(str(frame.data) == "A");

  bool has_next;
  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(str(frame.data) == "BB");

  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(str(frame.data) == "CCC");

  REQUIRE(a0_stream_has_next(lk, &has_next) == A0_OK);
  REQUIRE(!has_next);

  REQUIRE(a0_stream_jump_head(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(str(frame.data) == "A");

  REQUIRE(a0_stream_jump_tail(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(str(frame.data) == "CCC");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

void fork_sleep_push(a0_stream_t* stream, const std::string& str) {
  if (!fork()) {
    // Sleep for 1ms.
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1e6;  // 1ms
    nanosleep(&ts, (struct timespec*)NULL);

    a0_locked_stream_t lk;
    REQUIRE(a0_lock_stream(stream, &lk) == A0_OK);

    a0_stream_frame_t frame;
    REQUIRE(a0_stream_alloc(lk, 3, &frame) == A0_OK);
    memcpy(frame.data.ptr, str.c_str(), str.size());
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
  REQUIRE(str(frame.data) == "ABC");

  REQUIRE(a0_stream_await(lk, a0_stream_nonempty) == A0_OK);

  fork_sleep_push(&stream, "DEF");
  REQUIRE(a0_stream_await(lk, a0_stream_has_next) == A0_OK);

  REQUIRE(a0_stream_next(lk) == A0_OK);
  REQUIRE(a0_stream_frame(lk, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(str(frame.data) == "DEF");

  REQUIRE(a0_unlock_stream(lk) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}
