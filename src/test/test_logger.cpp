#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/pubsub.h>

#include <doctest.h>

#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>

#include "src/test_util.hpp"

TEST_CASE("logger] basic") {
  std::vector<uint8_t> heap_crit(1 * 1024 * 1024);
  std::vector<uint8_t> heap_err(1 * 1024 * 1024);
  std::vector<uint8_t> heap_warn(1 * 1024 * 1024);
  std::vector<uint8_t> heap_info(1 * 1024 * 1024);
  std::vector<uint8_t> heap_dbg(1 * 1024 * 1024);

  a0_arena_t arena_crit{.buf = {heap_crit.data(), heap_crit.size()}, .mode = A0_ARENA_MODE_SHARED};
  a0_arena_t arena_err{.buf = {heap_err.data(), heap_err.size()}, .mode = A0_ARENA_MODE_SHARED};
  a0_arena_t arena_warn{.buf = {heap_warn.data(), heap_warn.size()}, .mode = A0_ARENA_MODE_SHARED};
  a0_arena_t arena_info{.buf = {heap_info.data(), heap_info.size()}, .mode = A0_ARENA_MODE_SHARED};
  a0_arena_t arena_dbg{.buf = {heap_dbg.data(), heap_dbg.size()}, .mode = A0_ARENA_MODE_SHARED};

  a0_logger_t log;
  REQUIRE_OK(a0_logger_init(&log, arena_crit, arena_err, arena_warn, arena_info, arena_dbg));

  a0_packet_t pkt;
  a0_packet_init(&pkt);

  pkt.payload = a0::test::buf("crit");
  REQUIRE_OK(a0_log_crit(&log, pkt));

  pkt.payload = a0::test::buf("err");
  REQUIRE_OK(a0_log_err(&log, pkt));

  pkt.payload = a0::test::buf("warn");
  REQUIRE_OK(a0_log_warn(&log, pkt));

  pkt.payload = a0::test::buf("info");
  REQUIRE_OK(a0_log_info(&log, pkt));

  pkt.payload = a0::test::buf("dbg");
  REQUIRE_OK(a0_log_dbg(&log, pkt));

  auto REQUIRE_MSG = [](a0_arena_t arena, std::string msg) {
    a0_packet_t read_pkt;
    REQUIRE_OK(
        a0_subscriber_read_one(arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &read_pkt));
    REQUIRE(std::string((char*)read_pkt.payload.ptr) == msg);
  };

  REQUIRE_MSG(arena_crit, "crit");
  REQUIRE_MSG(arena_err, "err");
  REQUIRE_MSG(arena_warn, "warn");
  REQUIRE_MSG(arena_info, "info");
  REQUIRE_MSG(arena_dbg, "dbg");

  REQUIRE_OK(a0_logger_close(&log));

  REQUIRE(a0_logger_close(&log) == ESHUTDOWN);
  REQUIRE(a0_log_crit(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_err(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_warn(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_info(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_dbg(&log, pkt) == ESHUTDOWN);
}
