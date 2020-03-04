#include <a0/logger.h>
#include <a0/pubsub.h>

#include <doctest.h>
#include <errno.h>
#include <math.h>

#include <vector>

#include "src/test_util.hpp"

TEST_CASE("Test logger") {
  std::vector<uint8_t> arena_crit(1 * 1024 * 1024);
  std::vector<uint8_t> arena_err(1 * 1024 * 1024);
  std::vector<uint8_t> arena_warn(1 * 1024 * 1024);
  std::vector<uint8_t> arena_info(1 * 1024 * 1024);
  std::vector<uint8_t> arena_dbg(1 * 1024 * 1024);

  a0_buf_t buf_crit{arena_crit.data(), arena_crit.size()};
  a0_buf_t buf_err{arena_err.data(), arena_err.size()};
  a0_buf_t buf_warn{arena_warn.data(), arena_warn.size()};
  a0_buf_t buf_info{arena_info.data(), arena_info.size()};
  a0_buf_t buf_dbg{arena_dbg.data(), arena_dbg.size()};

  a0_logger_t log;
  REQUIRE_OK(a0_logger_init(&log, buf_crit, buf_err, buf_warn, buf_info, buf_dbg));

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

  auto REQUIRE_MSG = [](a0_buf_t arena, std::string msg) {
    a0_packet_t read_pkt;
    REQUIRE_OK(
        a0_subscriber_read_one(arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &read_pkt));
    REQUIRE(std::string((char*)read_pkt.payload.ptr) == msg);
  };

  REQUIRE_MSG(buf_crit, "crit");
  REQUIRE_MSG(buf_err, "err");
  REQUIRE_MSG(buf_warn, "warn");
  REQUIRE_MSG(buf_info, "info");
  REQUIRE_MSG(buf_dbg, "dbg");

  REQUIRE_OK(a0_logger_close(&log));

  REQUIRE(a0_logger_close(&log) == ESHUTDOWN);
  REQUIRE(a0_log_crit(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_err(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_warn(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_info(&log, pkt) == ESHUTDOWN);
  REQUIRE(a0_log_dbg(&log, pkt) == ESHUTDOWN);
}
