#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <doctest.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "src/c_wrap.hpp"
#include "src/err_macro.h"
#include "src/test_util.hpp"
#include "src/transport_debug.h"

static const char TEST_DISK[] = "/tmp/transport_test.a0";
static const char TEST_SHM[] = "transport_test.a0";
static const char TEST_SHM_ABS[] = "/dev/shm/alephzero/transport_test.a0";

static const char COPY_DISK[] = "/tmp/copy.a0";
static const char COPY_SHM[] = "copy.a0";
static const char COPY_SHM_ABS[] = "/dev/shm/alephzero/copy.a0";

struct TransportFixture {
  std::vector<uint8_t> stack_arena_data;
  a0_arena_t arena;

  a0_file_options_t diskopt;
  a0_file_t disk;

  a0_file_options_t shmopt;
  a0_file_t shm;

  TransportFixture() {
    stack_arena_data.resize(4096);
    arena = a0_arena_t{
        .buf = {stack_arena_data.data(), stack_arena_data.size()},
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

  ~TransportFixture() {
    a0_file_close(&disk);
    a0_file_remove(TEST_DISK);

    a0_file_close(&shm);
    a0_file_remove(TEST_SHM);
  }

  void require_debugstr(a0_transport_writer_locked_t* twl, const std::string& expected) {
    a0_buf_t debugstr;
    a0_transport_writer_debugstr(twl, &debugstr);
    REQUIRE(a0::test::str(debugstr) == expected);
    free(debugstr.data);
  }
};

TEST_CASE_FIXTURE(TransportFixture, "transport] construct") {
  a0_transport_writer_t tw;

  REQUIRE_OK(a0_transport_writer_init(&tw, arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    }
  },
  "data": [
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp construct") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    }
  },
  "data": [
  ]
}
)");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] alloc/commit") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  bool is_empty;
  REQUIRE_OK(a0_transport_writer_empty(&twl, &is_empty));
  REQUIRE(is_empty);

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    }
  },
  "data": [
  ]
}
)");

  a0_transport_frame_t first_frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 10, &first_frame));
  memcpy(first_frame.data, "0123456789", 10);
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  a0_transport_frame_t second_frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 40, &second_frame));
  memcpy(second_frame.data, "0123456789012345678901234567890123456789", 40);

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 642
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 656,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "committed": false,
      "off": 656,
      "seq": 2,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_commit(&twl));

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 656,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "off": 656,
      "seq": 2,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp alloc/commit") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  REQUIRE(twl.empty());

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    },
    "working_state": {
      "seq_low": 0,
      "seq_high": 0,
      "off_head": 0,
      "off_tail": 0,
      "high_water_mark": 592
    }
  },
  "data": [
  ]
}
)");

  a0::Frame first_frame = twl.alloc(10);
  memcpy(first_frame.data, "0123456789", 10);
  twl.commit();

  a0::Frame second_frame = twl.alloc(40);
  memcpy(second_frame.data, "0123456789012345678901234567890123456789", 40);

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 642
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 656,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "committed": false,
      "off": 656,
      "seq": 2,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");

  twl.commit();

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 656,
      "high_water_mark": 736
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 656,
      "data_size": 10,
      "data": "0123456789"
    },
    {
      "off": 656,
      "seq": 2,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 40,
      "data": "01234567890123456789012345678..."
    }
  ]
}
)");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] evicts") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  bool evicts;

  REQUIRE_OK(a0_transport_writer_alloc_evicts(&twl, 2 * 1024, &evicts));
  REQUIRE(!evicts);

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 2 * 1024, &frame));

  REQUIRE_OK(a0_transport_writer_alloc_evicts(&twl, 2 * 1024, &evicts));
  REQUIRE(evicts);

  REQUIRE(a0_transport_writer_alloc_evicts(&twl, 4 * 1024, &evicts) == A0_ERR_FRAME_LARGE);

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp evicts") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  REQUIRE(!twl.alloc_evicts(2 * 1024));
  twl.alloc(2 * 1024);
  REQUIRE(twl.alloc_evicts(2 * 1024));

  REQUIRE_THROWS_WITH(
      twl.alloc_evicts(4 * 1024),
      "Frame size too large");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] iteration") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));
  a0_transport_writer_locked_t twl;

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, arena));
  a0_transport_reader_locked_t trl;

  // Populate transport and release the write lock.
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  a0_transport_frame_t first_frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 1, &first_frame));
  memcpy(first_frame.data, "A", 1);

  a0_transport_frame_t second_frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 2, &second_frame));
  memcpy(second_frame.data, "BB", 2);

  a0_transport_frame_t third_frame;
  REQUIRE_OK(a0_transport_writer_alloc(&twl, 3, &third_frame));
  memcpy(third_frame.data, "CCC", 3);

  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_unlock(&twl));

  // Read and iterate the transport.
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  bool is_empty;
  REQUIRE_OK(a0_transport_reader_empty(&trl, &is_empty));
  REQUIRE(!is_empty);

  REQUIRE_OK(a0_transport_reader_jump_head(&trl));

  a0_transport_frame_t frame;

  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");
  size_t off_A = frame.hdr.off;

  bool has_next;
  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(has_next);

  bool has_prev;
  REQUIRE_OK(a0_transport_reader_has_prev(&trl, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_reader_step_next(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");
  size_t off_B = frame.hdr.off;

  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_reader_step_next(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");
  size_t off_C = frame.hdr.off;

  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(!has_next);

  REQUIRE_OK(a0_transport_reader_has_prev(&trl, &has_prev));
  REQUIRE(has_prev);

  REQUIRE_OK(a0_transport_reader_step_prev(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_transport_reader_step_prev(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_transport_reader_has_prev(&trl, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_reader_jump_tail(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  REQUIRE_OK(a0_transport_reader_jump_head(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_transport_reader_jump(&trl, off_A));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE_OK(a0_transport_reader_jump(&trl, off_B));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(a0::test::str(frame) == "BB");

  REQUIRE_OK(a0_transport_reader_jump(&trl, off_C));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(a0::test::str(frame) == "CCC");

  // Not aligned.
  REQUIRE(a0_transport_reader_jump(&trl, 13) == A0_ERR_RANGE);

  // Aligned.
  REQUIRE_OK(a0_transport_reader_jump(&trl, 2000));

  // Enough space for frame header.
  {
    REQUIRE_OK(a0_transport_reader_unlock(&trl));

    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));
    REQUIRE_OK(a0_transport_writer_resize(&twl, 2000 + sizeof(a0_transport_frame_hdr_t) + 1));
    REQUIRE_OK(a0_transport_writer_unlock(&twl));

    REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));
  }
  REQUIRE_OK(a0_transport_reader_jump(&trl, 2000));

  // Not enough space for frame header.
  {
    REQUIRE_OK(a0_transport_reader_unlock(&trl));

    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));
    REQUIRE_OK(a0_transport_writer_resize(&twl, 2000 + sizeof(a0_transport_frame_hdr_t)));
    REQUIRE_OK(a0_transport_writer_unlock(&twl));

    REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));
  }
  REQUIRE(a0_transport_reader_jump(&trl, 2000) == A0_ERR_RANGE);

  // Enough space for frame body.
  {
    REQUIRE_OK(a0_transport_reader_unlock(&trl));

    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));
    REQUIRE_OK(a0_transport_writer_resize(&twl, 2000 + sizeof(a0_transport_frame_hdr_t) + 1));
    REQUIRE_OK(a0_transport_writer_unlock(&twl));

    REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));
  }
  auto* frame_hdr = (a0_transport_frame_hdr_t*)(trl._tr->_arena.buf.data + 2000);
  REQUIRE(!frame_hdr->data_size);

  // Not enough space for frame body.
  frame_hdr->data_size = 1;
  REQUIRE(a0_transport_reader_jump(&trl, 2000) == A0_ERR_RANGE);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp iteration") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  // Populate transport and release the write lock.
  auto twl = tw.lock();

  auto first_frame = twl.alloc(1);
  memcpy(first_frame.data, "A", 1);
  twl.commit();

  auto second_frame = twl.alloc(2);
  memcpy(second_frame.data, "BB", 2);
  twl.commit();

  auto third_frame = twl.alloc(3);
  memcpy(third_frame.data, "CCC", 3);
  twl.commit();

  twl = {};

  // Read and iterate the transport.
  auto trl = tr.lock();

  REQUIRE(!trl.empty());

  trl.jump_head();
  auto frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");
  size_t off_A = frame.hdr.off;

  REQUIRE(trl.has_next());
  REQUIRE(!trl.has_prev());

  trl.step_next();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");
  size_t off_B = frame.hdr.off;

  REQUIRE(trl.has_next());

  trl.step_next();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");
  size_t off_C = frame.hdr.off;

  REQUIRE(!trl.has_next());
  REQUIRE(trl.has_prev());

  trl.step_prev();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "BB");

  trl.step_prev();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  REQUIRE(!trl.has_prev());

  trl.jump_tail();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 3);
  REQUIRE(a0::test::str(frame) == "CCC");

  trl.jump_head();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "A");

  trl.jump(off_A);
  REQUIRE(a0::test::str(trl.frame()) == "A");

  trl.jump(off_B);
  REQUIRE(a0::test::str(trl.frame()) == "BB");

  trl.jump(off_C);
  REQUIRE(a0::test::str(trl.frame()) == "CCC");

  // Not aligned.
  REQUIRE_THROWS_WITH(
      trl.jump(13),
      "Index out of bounds");

  // Aligned.
  trl.jump(2000);

  // Enough space for frame header.
  trl = {};
  twl = tw.lock();
  twl.resize(2000 + sizeof(a0_transport_frame_hdr_t) + 1);
  twl = {};
  trl = tr.lock();
  trl.jump(2000);

  // Not enough space for frame header.
  trl = {};
  twl = tw.lock();
  twl.resize(2000 + sizeof(a0_transport_frame_hdr_t));
  twl = {};
  trl = tr.lock();
  REQUIRE_THROWS_WITH(
      trl.jump(2000),
      "Index out of bounds");

  // Enough space for frame body.
  trl = {};
  twl = tw.lock();
  twl.resize(2000 + sizeof(a0_transport_frame_hdr_t) + 1);
  twl = {};
  trl = tr.lock();
  auto* frame_hdr = (a0_transport_frame_hdr_t*)(trl.c->_tr->_arena.buf.data + 2000);
  REQUIRE(!frame_hdr->data_size);

  // Not enough space for frame body.
  frame_hdr->data_size = 1;
  REQUIRE_THROWS_WITH(
      trl.jump(2000),
      "Index out of bounds");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] empty jumps") {
  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, arena));

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  REQUIRE(a0_transport_reader_jump_head(&trl) == A0_ERR_RANGE);
  REQUIRE(a0_transport_reader_jump_tail(&trl) == A0_ERR_RANGE);
  REQUIRE(a0_transport_reader_step_next(&trl) == A0_ERR_RANGE);
  REQUIRE(a0_transport_reader_step_prev(&trl) == A0_ERR_RANGE);

  bool has_next;
  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(!has_next);

  bool has_prev;
  REQUIRE_OK(a0_transport_reader_has_prev(&trl, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp empty jumps") {
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  auto trl = tr.lock();

  REQUIRE_THROWS_WITH(
      trl.jump_head(),
      "Index out of bounds");

  REQUIRE_THROWS_WITH(
      trl.jump_tail(),
      "Index out of bounds");

  REQUIRE_THROWS_WITH(
      trl.step_prev(),
      "Index out of bounds");

  REQUIRE_THROWS_WITH(
      trl.step_next(),
      "Index out of bounds");

  REQUIRE(!trl.has_next());
  REQUIRE(!trl.has_prev());
}

TEST_CASE_FIXTURE(TransportFixture, "transport] wrap around") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  std::string data(1 * 1024, 'a');  // 1kB string
  for (int i = 0; i < 20; i++) {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
    memcpy(frame.data, data.c_str(), data.size());
  }

  REQUIRE_OK(a0_transport_writer_commit(&twl));

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2736,
      "off_tail": 1664,
      "high_water_mark": 3800
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2736,
      "off_tail": 1664,
      "high_water_mark": 3800
    }
  },
  "data": [
    {
      "off": 2736,
      "seq": 18,
      "prev_off": 1664,
      "next_off": 592,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 592,
      "seq": 19,
      "prev_off": 2736,
      "next_off": 1664,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 1664,
      "seq": 20,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp wrap around") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  std::string data(1 * 1024, 'a');  // 1kB string
  for (int i = 0; i < 20; i++) {
    auto frame = twl.alloc(data.size());
    memcpy(frame.data, data.c_str(), data.size());
  }

  twl.commit();

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2736,
      "off_tail": 1664,
      "high_water_mark": 3800
    },
    "working_state": {
      "seq_low": 18,
      "seq_high": 20,
      "off_head": 2736,
      "off_tail": 1664,
      "high_water_mark": 3800
    }
  },
  "data": [
    {
      "off": 2736,
      "seq": 18,
      "prev_off": 1664,
      "next_off": 592,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 592,
      "seq": 19,
      "prev_off": 2736,
      "next_off": 1664,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    },
    {
      "off": 1664,
      "seq": 20,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 1024,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] expired next") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));
  a0_transport_writer_locked_t twl;

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, arena));
  a0_transport_reader_locked_t trl;

  a0_transport_frame_t frame;
  std::string data(1 * 1024, 'a');  // 1kB string

  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  REQUIRE_OK(a0_transport_reader_jump_head(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  for (int i = 0; i < 20; i++) {
    REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
    memcpy(frame.data, data.c_str(), data.size());
    REQUIRE_OK(a0_transport_writer_commit(&twl));
  }

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  bool valid;
  REQUIRE_OK(a0_transport_reader_iter_valid(&trl, &valid));
  REQUIRE(!valid);

  bool has_next;
  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_reader_step_next(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 19);

  REQUIRE_OK(a0_transport_reader_iter_valid(&trl, &valid));
  REQUIRE(valid);

  bool has_prev;
  REQUIRE_OK(a0_transport_reader_has_prev(&trl, &has_prev));
  REQUIRE(!has_prev);

  REQUIRE_OK(a0_transport_reader_has_next(&trl, &has_next));
  REQUIRE(has_next);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp expired next") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  std::string data(1 * 1024, 'a');  // 1kB string
  auto frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  twl = {};

  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  auto trl = tr.lock();

  trl.jump_head();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);

  trl = {};

  twl = tw.lock();

  for (int i = 0; i < 20; i++) {
    auto frame = twl.alloc(data.size());
    memcpy(frame.data, data.c_str(), data.size());
    twl.commit();
  }

  twl = {};

  trl = tr.lock();

  REQUIRE(!trl.iter_valid());
  REQUIRE(trl.has_next());

  trl.step_next();
  REQUIRE(trl.iter_valid());
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 19);

  REQUIRE(!trl.has_prev());
  REQUIRE(trl.has_next());
}

TEST_CASE_FIXTURE(TransportFixture, "transport] large alloc") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  std::string long_str(3 * 1024, 'a');  // 3kB string
  for (int i = 0; i < 5; i++) {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_writer_alloc(&twl, long_str.size(), &frame));
    memcpy(frame.data, long_str.c_str(), long_str.size());
    REQUIRE_OK(a0_transport_writer_commit(&twl));
  }

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 3704
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 3704
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 5,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 3072,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp large alloc") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  std::string long_str(3 * 1024, 'a');  // 3kB string
  for (int i = 0; i < 5; i++) {
    auto frame = twl.alloc(long_str.size());
    memcpy(frame.data, long_str.c_str(), long_str.size());
    twl.commit();
  }

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 3704
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 5,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 3704
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 5,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 3072,
      "data": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa..."
    }
  ]
}
)");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] resize") {
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, arena));
  a0_transport_writer_locked_t twl;

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, arena));
  a0_transport_reader_locked_t trl;

  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  size_t used_space;
  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 592);

  std::string data(1024, 'a');
  a0_transport_frame_t frame;

  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 1656);

  REQUIRE(a0_transport_writer_resize(&twl, 0) == A0_ERR_INVALID_ARG);
  REQUIRE(a0_transport_writer_resize(&twl, 1207) == A0_ERR_INVALID_ARG);
  REQUIRE_OK(a0_transport_writer_resize(&twl, 1656));

  data = std::string(1024 + 1, 'a');  // 1 byte larger than previous.
  REQUIRE(a0_transport_writer_alloc(&twl, data.size(), &frame) == A0_ERR_FRAME_LARGE);

  data = std::string(1024, 'b');  // same size as existing.
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  REQUIRE_OK(a0_transport_reader_jump_tail(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.data_size == 1024);
  REQUIRE(a0::test::str(frame) == data);
  REQUIRE(arena.buf.data[1207] == 'b');
  REQUIRE(arena.buf.data[1656] != 'b');

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 1656,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 1656
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 1656
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 2,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 1656);

  REQUIRE_OK(a0_transport_writer_resize(&twl, 4096));

  data = std::string(2 * 1024, 'c');
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 3752);

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 592,
      "off_tail": 1664,
      "high_water_mark": 3752
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 592,
      "off_tail": 1664,
      "high_water_mark": 3752
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 2,
      "prev_off": 0,
      "next_off": 1664,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    },
    {
      "off": 1664,
      "seq": 3,
      "prev_off": 592,
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
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 3816);

  data = std::string(3129, 'e');
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 3761);

  data = std::string(16, 'f');
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 3832);

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 592,
      "off_tail": 3776,
      "high_water_mark": 3832
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 592,
      "off_tail": 3776,
      "high_water_mark": 3832
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 5,
      "prev_off": 0,
      "next_off": 3776,
      "data_size": 3129,
      "data": "eeeeeeeeeeeeeeeeeeeeeeeeeeeee..."
    },
    {
      "off": 3776,
      "seq": 6,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 16,
      "data": "ffffffffffffffff"
    }
  ]
}
)");

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with less data.
  data = std::string(3264, 'g');
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  memcpy(frame.data, data.c_str(), data.size());
  REQUIRE_OK(a0_transport_writer_commit(&twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == (592 + 40 + 3264));

  uint64_t seq_low;
  REQUIRE_OK(a0_transport_writer_seq_low(&twl, &seq_low));
  REQUIRE(seq_low == 7);

  uint64_t seq_high;
  REQUIRE_OK(a0_transport_writer_seq_high(&twl, &seq_high));
  REQUIRE(seq_high == 7);

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with nothing.
  REQUIRE_OK(a0_transport_writer_alloc(&twl, data.size(), &frame));
  REQUIRE_OK(a0_transport_writer_unlock(&twl));
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  REQUIRE_OK(a0_transport_writer_used_space(&twl, &used_space));
  REQUIRE(used_space == 592);

  REQUIRE_OK(a0_transport_writer_seq_low(&twl, &seq_low));
  REQUIRE(seq_low == 8);

  REQUIRE_OK(a0_transport_writer_seq_high(&twl, &seq_high));
  REQUIRE(seq_high == 7);

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp resize") {
  a0::TransportWriter tw(a0::cpp_wrap<a0::Arena>(arena));
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  auto twl = tw.lock();

  REQUIRE(twl.used_space() == 592);

  std::string data(1024, 'a');
  auto frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == 1656);

  REQUIRE_THROWS_WITH(
      twl.resize(0),
      "Invalid argument");

  REQUIRE_THROWS_WITH(
      twl.resize(1207),
      "Invalid argument");

  twl.resize(1656);

  data = std::string(1024 + 1, 'a');  // 1 byte larger than previous.

  REQUIRE_THROWS_WITH(
      twl.resize(data.size()),
      "Invalid argument");

  REQUIRE_THROWS_WITH(
      twl.alloc(data.size()),
      "Frame size too large");

  data = std::string(1024, 'b');  // same size as existing.
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  twl = {};
  auto trl = tr.lock();
  trl.jump_tail();
  frame = trl.frame();
  REQUIRE(frame.hdr.data_size == 1024);
  REQUIRE(a0::test::str(frame) == data);
  REQUIRE(arena.buf.data[1207] == 'b');
  REQUIRE(arena.buf.data[1656] != 'b');
  trl = {};
  twl = tw.lock();

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 1656,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 1656
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 1656
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 2,
      "prev_off": 0,
      "next_off": 0,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    }
  ]
}
)");

  REQUIRE(twl.used_space() == 1656);

  twl.resize(4096);

  data = std::string(2 * 1024, 'c');
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == 3752);

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 592,
      "off_tail": 1664,
      "high_water_mark": 3752
    },
    "working_state": {
      "seq_low": 2,
      "seq_high": 3,
      "off_head": 592,
      "off_tail": 1664,
      "high_water_mark": 3752
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 2,
      "prev_off": 0,
      "next_off": 1664,
      "data_size": 1024,
      "data": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbb..."
    },
    {
      "off": 1664,
      "seq": 3,
      "prev_off": 592,
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
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == 3816);

  data = std::string(3129, 'e');
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == 3761);

  data = std::string(16, 'f');
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == 3832);

  require_debugstr(&*twl.c, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 592,
      "off_tail": 3776,
      "high_water_mark": 3832
    },
    "working_state": {
      "seq_low": 5,
      "seq_high": 6,
      "off_head": 592,
      "off_tail": 3776,
      "high_water_mark": 3832
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 5,
      "prev_off": 0,
      "next_off": 3776,
      "data_size": 3129,
      "data": "eeeeeeeeeeeeeeeeeeeeeeeeeeeee..."
    },
    {
      "off": 3776,
      "seq": 6,
      "prev_off": 592,
      "next_off": 0,
      "data_size": 16,
      "data": "ffffffffffffffff"
    }
  ]
}
)");

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with less data.
  data = std::string(3264, 'g');
  frame = twl.alloc(data.size());
  memcpy(frame.data, data.c_str(), data.size());
  twl.commit();

  REQUIRE(twl.used_space() == (592 + 40 + 3264));

  REQUIRE(twl.seq_low() == 7);
  REQUIRE(twl.seq_high() == 7);

  // This forces an eviction of all existing data, reducing the high water mark.
  // We replace it with nothing.
  frame = twl.alloc(data.size());

  twl = {};
  twl = tw.lock();

  REQUIRE(twl.used_space() == 592);

  REQUIRE(twl.seq_low() == 8);
  REQUIRE(twl.seq_high() == 7);
}

TEST_CASE_FIXTURE(TransportFixture, "transport] timedwait") {
  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, arena));

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  a0_time_mono_t now;
  a0_time_mono_now(&now);

  REQUIRE(A0_SYSERR(a0_transport_reader_timedwait(&trl, a0_transport_reader_nonempty_pred(&trl), now)) == ETIMEDOUT);

  a0_time_mono_t fut;
  a0_time_mono_add(now, 1e6, &fut);
  REQUIRE(A0_SYSERR(a0_transport_reader_timedwait(&trl, a0_transport_reader_nonempty_pred(&trl), fut)) == ETIMEDOUT);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp timedwait") {
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  auto trl = tr.lock();

  REQUIRE_THROWS_WITH(
      trl.wait_for([]() { return false; }, std::chrono::nanoseconds(0)),
      strerror(ETIMEDOUT));

  REQUIRE_THROWS_WITH(
      trl.wait_for([]() { return false; }, std::chrono::nanoseconds((uint64_t)1e6)),
      strerror(ETIMEDOUT));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp pred throws") {
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(arena));
  auto trl = tr.lock();

  std::string want_throw(1023, 'x');
  REQUIRE_THROWS_WITH(
      trl.wait([]() -> bool { throw std::runtime_error(std::string(2048, 'x')); }),
      want_throw.c_str());
}

void fork_sleep_push(a0_arena_t arena, const std::string& str) {
  if (!fork()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    a0_transport_writer_t tw;
    REQUIRE_OK(a0_transport_writer_init(&tw, arena));

    a0_transport_writer_locked_t twl;
    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_writer_alloc(&twl, str.size(), &frame));
    memcpy(frame.data, str.c_str(), str.size());
    REQUIRE_OK(a0_transport_writer_commit(&twl));

    REQUIRE_OK(a0_transport_writer_unlock(&twl));

    exit(0);
  }
}

TEST_CASE_FIXTURE(TransportFixture, "transport] disk await") {
  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, disk.arena));

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  fork_sleep_push(disk.arena, "ABC");
  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_nonempty_pred(&trl)));

  REQUIRE_OK(a0_transport_reader_jump_head(&trl));

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_nonempty_pred(&trl)));

  fork_sleep_push(disk.arena, "DEF");
  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_has_next_pred(&trl)));

  REQUIRE_OK(a0_transport_reader_step_next(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_transport_reader_shutdown(&trl));
  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp disk await") {
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(disk.arena));
  auto trl = tr.lock();

  fork_sleep_push(disk.arena, "ABC");
  trl.wait([&]() { return !trl.empty(); });

  trl.jump_head();

  auto frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  trl.wait([&]() { return !trl.empty(); });

  fork_sleep_push(disk.arena, "DEF");

  trl.wait([&]() { return trl.has_next(); });

  trl.step_next();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] shm await") {
  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, shm.arena));

  fork_sleep_push(shm.arena, "ABC");

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_nonempty_pred(&trl)));

  REQUIRE_OK(a0_transport_reader_jump_head(&trl));

  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_nonempty_pred(&trl)));

  fork_sleep_push(shm.arena, "DEF");
  REQUIRE_OK(a0_transport_reader_wait(&trl, a0_transport_reader_has_next_pred(&trl)));

  REQUIRE_OK(a0_transport_reader_step_next(&trl));
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");

  REQUIRE_OK(a0_transport_reader_shutdown(&trl));
  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] cpp shm await") {
  a0::TransportReader tr(a0::cpp_wrap<a0::Arena>(shm.arena));

  fork_sleep_push(shm.arena, "ABC");

  auto trl = tr.lock();

  trl.wait([&]() { return !trl.empty(); });

  trl.jump_head();

  auto frame = trl.frame();
  REQUIRE(frame.hdr.seq == 1);
  REQUIRE(a0::test::str(frame) == "ABC");

  trl.wait([&]() { return !trl.empty(); });

  fork_sleep_push(shm.arena, "DEF");

  trl.wait([&]() { return trl.has_next(); });

  trl.step_next();
  frame = trl.frame();
  REQUIRE(frame.hdr.seq == 2);
  REQUIRE(a0::test::str(frame) == "DEF");
}

TEST_CASE_FIXTURE(TransportFixture, "transport] robust") {
  int child_pid = fork();
  if (!child_pid) {
    a0_transport_writer_t tw;
    REQUIRE_OK(a0_transport_writer_init(&tw, shm.arena));

    // Write one frame successfully.
    {
      a0_transport_writer_locked_t twl;
      REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_writer_alloc(&twl, 3, &frame));
      memcpy(frame.data, "YES", 3);
      REQUIRE_OK(a0_transport_writer_commit(&twl));

      REQUIRE_OK(a0_transport_writer_unlock(&twl));
    }

    // Write one frame unsuccessfully.
    {
      a0_transport_writer_locked_t twl;
      REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_writer_alloc(&twl, 2, &frame));
      memcpy(frame.data, "NO", 2);

      require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 635
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 2,
      "off_head": 592,
      "off_tail": 640,
      "high_water_mark": 682
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 640,
      "data_size": 3,
      "data": "YES"
    },
    {
      "committed": false,
      "off": 640,
      "seq": 2,
      "prev_off": 592,
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

  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, shm.arena));

  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

  require_debugstr(&twl, R"(
{
  "header": {
    "arena_size": 4096,
    "committed_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 635
    },
    "working_state": {
      "seq_low": 1,
      "seq_high": 1,
      "off_head": 592,
      "off_tail": 592,
      "high_water_mark": 635
    }
  },
  "data": [
    {
      "off": 592,
      "seq": 1,
      "prev_off": 0,
      "next_off": 640,
      "data_size": 3,
      "data": "YES"
    }
  ]
}
)");

  REQUIRE_OK(a0_transport_writer_unlock(&twl));
}

TEST_CASE_FIXTURE(TransportFixture, "transport] robust fuzz") {
  int child_pid = fork();
  if (!child_pid) {
    a0_transport_writer_t tw;
    REQUIRE_OK(a0_transport_writer_init(&tw, shm.arena));

    while (true) {
      a0_transport_writer_locked_t twl;
      REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

      auto str = a0::test::random_string(rand() % 1024);

      a0_transport_frame_t frame;
      REQUIRE_OK(a0_transport_writer_alloc(&twl, str.size(), &frame));
      memcpy(frame.data, str.c_str(), str.size());
      REQUIRE_OK(a0_transport_writer_commit(&twl));

      REQUIRE_OK(a0_transport_writer_unlock(&twl));
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
  a0_transport_writer_t tw;
  REQUIRE_OK(a0_transport_writer_init(&tw, shm.arena));

  // Make sure the transport is still functinal.
  // We can still grab the lock, write, and read from the transport.
  a0_transport_writer_locked_t twl;
  REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));
  {
    a0_transport_frame_t frame;
    REQUIRE_OK(a0_transport_writer_alloc(&twl, 11, &frame));
    memcpy(frame.data, "Still Works", 11);
    REQUIRE_OK(a0_transport_writer_commit(&twl));
  }
  REQUIRE_OK(a0_transport_writer_unlock(&twl));

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, shm.arena));
  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  REQUIRE_OK(a0_transport_reader_jump_tail(&trl));
  a0_transport_frame_t frame;
  REQUIRE_OK(a0_transport_reader_frame(&trl, &frame));
  REQUIRE(a0::test::str(frame) == "Still Works");

  REQUIRE_OK(a0_transport_reader_unlock(&trl));
}

void copy_file(const std::string& from, const std::string& to) {
  std::ifstream src(from.data(), std::ios::binary);
  std::ofstream dst(to.data(), std::ios::binary);

  dst << src.rdbuf();
}

TEST_CASE_FIXTURE(TransportFixture, "transport] robust copy shm->disk->shm") {
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);

  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_transport_writer_t tw;
    REQUIRE_OK(a0_transport_writer_init(&tw, shm.arena));

    a0_transport_writer_locked_t twl;
    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

    a0_transport_frame_t frame;
    a0_transport_writer_alloc(&twl, str.size(), &frame);
    memcpy(frame.data, str.c_str(), str.size());
    a0_transport_writer_commit(&twl);

    // Do not unlock!
    // a0_transport_writer_unlock(&twl);

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

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, copied_shm.arena));

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  a0_transport_reader_jump_head(&trl);
  a0_transport_frame_t frame;
  a0_transport_reader_frame(&trl, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));

  a0_file_remove(COPY_DISK);
  a0_file_close(&copied_shm);
  a0_file_remove(COPY_SHM);
}

TEST_CASE_FIXTURE(TransportFixture, "transport] robust copy disk->shm->disk") {
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);

  std::string str = "Original String";

  int child_pid = fork();
  if (!child_pid) {
    a0_transport_writer_t tw;
    REQUIRE_OK(a0_transport_writer_init(&tw, disk.arena));

    a0_transport_writer_locked_t twl;
    REQUIRE_OK(a0_transport_writer_lock(&tw, &twl));

    a0_transport_frame_t frame;
    a0_transport_writer_alloc(&twl, str.size(), &frame);
    memcpy(frame.data, str.c_str(), str.size());
    a0_transport_writer_commit(&twl);

    // Do not unlock!
    // a0_transport_unlock(&twl);

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

  a0_transport_reader_t tr;
  REQUIRE_OK(a0_transport_reader_init(&tr, copied_disk.arena));

  a0_transport_reader_locked_t trl;
  REQUIRE_OK(a0_transport_reader_lock(&tr, &trl));

  a0_transport_reader_jump_head(&trl);
  a0_transport_frame_t frame;
  a0_transport_reader_frame(&trl, &frame);
  REQUIRE(a0::test::str(frame) == str);

  REQUIRE_OK(a0_transport_reader_unlock(&trl));

  a0_file_close(&copied_disk);
  a0_file_remove(COPY_DISK);
  a0_file_remove(COPY_SHM);
}
