#include <a0/stream.h>

#include <a0/internal/stream_debug.h>
#include <a0/internal/test_util.hh>
#include <catch.hpp>
#include <string.h>
#include <unistd.h>

static const char* TEST_SHM = "/test.shm";

struct StreamTestFixture {
  StreamTestFixture() {
    a0_shmobj_unlink(TEST_SHM);
    shmopt.size = 4096;
    a0_shmobj_open(TEST_SHM, &shmopt, &shmobj);
  }
  ~StreamTestFixture() {
    a0_shmobj_close(&shmobj);
    a0_shmobj_unlink(TEST_SHM);
  }

  a0_shmobj_options_t shmopt;
  a0_shmobj_t shmobj;

  uint32_t construct_cnt{ 0 };
  uint32_t already_constructed_cnt{ 0 };
};

TEST_CASE_METHOD(StreamTestFixture,
                 "Test stream construct",
                 "[stream_construct]") {
  REQUIRE(*(uint64_t*)shmobj.ptr != 0xA0A0A0A0A0A0A0A0);

  a0_stream_construct_options_t sco;
  memset(&sco, 0, sizeof(sco));
  sco.protocol_metadata_size = 13;
  sco.on_construct = [](a0_stream_t* stream) {
    ((StreamTestFixture*)(stream->user_data))->construct_cnt++;
  };
  sco.on_already_constructed = [](a0_stream_t* stream) {
    ((StreamTestFixture*)(stream->user_data))->already_constructed_cnt++;
  };

  a0_stream_t stream;
  stream.shmobj = &shmobj;
  stream.user_data = this;
  REQUIRE(a0_stream_init(&stream, &sco) == A0_OK);

  REQUIRE(*(uint64_t*)shmobj.ptr == 0xA0A0A0A0A0A0A0A0);
  REQUIRE(construct_cnt == 1);
  REQUIRE(already_constructed_cnt == 0);

  REQUIRE(a0_stream_init(&stream, &sco) == A0_OK);
  REQUIRE(construct_cnt == 1);
  REQUIRE(already_constructed_cnt == 1);

  REQUIRE(a0_stream_init(&stream, &sco) == A0_OK);
  REQUIRE(construct_cnt == 1);
  REQUIRE(already_constructed_cnt == 2);

  a0_locked_stream_t locked_stream;
  REQUIRE(a0_lock_stream(&stream, &locked_stream) == A0_OK);

  a0_buf_t protocol_metadata;
  REQUIRE(a0_stream_protocol_metadata(locked_stream, &protocol_metadata) == A0_OK);
  REQUIRE(protocol_metadata.size == 13);
  REQUIRE((uintptr_t)protocol_metadata.ptr % alignof(max_align_t) == 0);
  memcpy(protocol_metadata.ptr, "protocol info", 13);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(locked_stream, &debugstr);
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
-- size = 13
-- payload: protocol info
=========================
DATA
=========================
)");
    free(debugstr.ptr);
  }

  REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

TEST_CASE_METHOD(StreamTestFixture,
                 "Test stream alloc/commit",
                 "[stream_alloc_commit]") {
  a0_stream_t stream;
  stream.shmobj = &shmobj;
  REQUIRE(a0_stream_init(&stream, nullptr) == A0_OK);

  a0_locked_stream_t locked_stream;
  REQUIRE(a0_lock_stream(&stream, &locked_stream) == A0_OK);

  bool is_empty;
  REQUIRE(a0_stream_empty(locked_stream, &is_empty) == A0_OK);
  REQUIRE(is_empty);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(locked_stream, &debugstr);
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
-- size = 0
-- payload: 
=========================
DATA
=========================
)");
    free(debugstr.ptr);
  }

  a0_stream_frame_t first_frame;
  REQUIRE(a0_stream_alloc(locked_stream, 10, &first_frame) == A0_OK);
  memcpy(first_frame.data.ptr, "0123456789", 10);
  REQUIRE(a0_stream_commit(locked_stream) == A0_OK);

  a0_stream_frame_t second_frame;
  REQUIRE(a0_stream_alloc(locked_stream, 40, &second_frame) == A0_OK);
  memcpy(second_frame.data.ptr, "0123456789012345678901234567890123456789", 40);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(locked_stream, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [1, 1]
-- head @ = 144
-- tail @ = 144
-------------------------
Working state
-- seq    = [1, 2]
-- head @ = 144
-- tail @ = 192
=========================
PROTOCOL INFO
-------------------------
-- size = 0
-- payload: 
=========================
DATA
-------------------------
Elem
-- @      = 144
-- seq    = 1
-- next @ = 192
-- size   = 10
-- payload: 0123456789
-------------------------
Elem (not committed)
-- @      = 192
-- seq    = 2
-- next @ = 0
-- size   = 40
-- payload: 01234567890123456789012345678...
=========================
)");
    free(debugstr.ptr);
  }

  REQUIRE(a0_stream_commit(locked_stream) == A0_OK);

  {
    a0_buf_t debugstr;
    a0_stream_debugstr(locked_stream, &debugstr);
    REQUIRE(str(debugstr) == R"(
=========================
HEADER
-------------------------
-- shmobj_size = 4096
-------------------------
Committed state
-- seq    = [1, 2]
-- head @ = 144
-- tail @ = 192
-------------------------
Working state
-- seq    = [1, 2]
-- head @ = 144
-- tail @ = 192
=========================
PROTOCOL INFO
-------------------------
-- size = 0
-- payload: 
=========================
DATA
-------------------------
Elem
-- @      = 144
-- seq    = 1
-- next @ = 192
-- size   = 10
-- payload: 0123456789
-------------------------
Elem
-- @      = 192
-- seq    = 2
-- next @ = 0
-- size   = 40
-- payload: 01234567890123456789012345678...
=========================
)");
    free(debugstr.ptr);
  }

  REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}
TEST_CASE_METHOD(StreamTestFixture, "Test stream iteration", "[stream_iter]") {
  // Create stream and close it.
  {
    a0_stream_t stream;
    stream.shmobj = &shmobj;
    REQUIRE(a0_stream_init(&stream, nullptr) == A0_OK);

    a0_locked_stream_t locked_stream;
    REQUIRE(a0_lock_stream(&stream, &locked_stream) == A0_OK);

    a0_stream_frame_t first_frame;
    REQUIRE(a0_stream_alloc(locked_stream, 1, &first_frame) == A0_OK);
    memcpy(first_frame.data.ptr, "A", 1);

    a0_stream_frame_t second_frame;
    REQUIRE(a0_stream_alloc(locked_stream, 2, &second_frame) == A0_OK);
    memcpy(second_frame.data.ptr, "BB", 2);

    a0_stream_frame_t third_frame;
    REQUIRE(a0_stream_alloc(locked_stream, 3, &third_frame) == A0_OK);
    memcpy(third_frame.data.ptr, "CCC", 3);

    REQUIRE(a0_stream_commit(locked_stream) == A0_OK);

    REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
    REQUIRE(a0_stream_close(&stream) == A0_OK);
  }

  a0_stream_t stream;
  stream.shmobj = &shmobj;
  REQUIRE(a0_stream_init(&stream, nullptr) == A0_OK);

  a0_locked_stream_t locked_stream;
  REQUIRE(a0_lock_stream(&stream, &locked_stream) == A0_OK);

  bool is_empty;
  REQUIRE(a0_stream_empty(locked_stream, &is_empty) == A0_OK);
  REQUIRE(!is_empty);

  REQUIRE(a0_stream_jump_head(locked_stream) == A0_OK);

  a0_stream_frame_t frame;

  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(str(frame.data) == "A");

  bool has_next;
  REQUIRE(a0_stream_has_next(locked_stream, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(locked_stream) == A0_OK);
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(str(frame.data) == "BB");

  REQUIRE(a0_stream_has_next(locked_stream, &has_next) == A0_OK);
  REQUIRE(has_next);

  REQUIRE(a0_stream_next(locked_stream) == A0_OK);
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(str(frame.data) == "CCC");

  REQUIRE(a0_stream_has_next(locked_stream, &has_next) == A0_OK);
  REQUIRE(!has_next);

  REQUIRE(a0_stream_jump_head(locked_stream) == A0_OK);
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(str(frame.data) == "A");

  REQUIRE(a0_stream_jump_tail(locked_stream) == A0_OK);
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(str(frame.data) == "CCC");

  REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}

void _fork_sleep_push(a0_stream_t* stream, const std::string& str) {
  if (!fork()) {
    // Sleep for 1ms.
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1e6;  // 1ms
    nanosleep(&ts, (struct timespec *)NULL);

    a0_locked_stream_t locked_stream;
    REQUIRE(a0_lock_stream(stream, &locked_stream) == A0_OK);

    a0_stream_frame_t frame;
    REQUIRE(a0_stream_alloc(locked_stream, 3, &frame) == A0_OK);
    memcpy(frame.data.ptr, str.c_str(), str.size());
    REQUIRE(a0_stream_commit(locked_stream) == A0_OK);

    REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
    REQUIRE(a0_stream_close(stream) == A0_OK);

    exit(0);
  }
}

TEST_CASE_METHOD(StreamTestFixture, "Test stream await", "[stream_await]") {
  a0_stream_t stream;
  stream.shmobj = &shmobj;
  REQUIRE(a0_stream_init(&stream, nullptr) == A0_OK);

  _fork_sleep_push(&stream, "ABC");

  a0_locked_stream_t locked_stream;
  REQUIRE(a0_lock_stream(&stream, &locked_stream) == A0_OK);

  REQUIRE(a0_stream_await(locked_stream, a0_stream_nonempty) == A0_OK);

  REQUIRE(a0_stream_jump_head(locked_stream) == A0_OK);

  a0_stream_frame_t frame;
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(str(frame.data) == "ABC");

  REQUIRE(a0_stream_await(locked_stream, a0_stream_nonempty) == A0_OK);

  _fork_sleep_push(&stream, "DEF");
  REQUIRE(a0_stream_await(locked_stream, a0_stream_has_next) == A0_OK);

  REQUIRE(a0_stream_next(locked_stream) == A0_OK);
  REQUIRE(a0_stream_frame(locked_stream, &frame) == A0_OK);
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(str(frame.data) == "DEF");

  REQUIRE(a0_unlock_stream(locked_stream) == A0_OK);
  REQUIRE(a0_stream_close(&stream) == A0_OK);
}
