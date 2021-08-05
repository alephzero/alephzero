#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/log.h>
#include <a0/packet.h>
#include <a0/pubsub.h>

#include <doctest.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "src/test_util.hpp"

struct LogFixture {
  a0_log_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.log.a0";

  LogFixture() {
    clear();
  }

  ~LogFixture() {
    clear();
  }

  void clear() {
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(LogFixture, "logger] basic") {
  std::map<std::string, size_t> counter;

  a0_packet_callback_t onmsg = {
      .user_data = &counter,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* cnt = (std::map<std::string, size_t>*)user_data;

            auto hdr = a0::test::hdr(pkt);
            auto range = hdr.equal_range("a0_level");
            for (auto it = range.first; it != range.second; ++it) {
              (*cnt)[it->second]++;
            }
          },
  };

  a0_log_listener_t log_list;
  REQUIRE_OK(a0_log_listener_init(&log_list, topic, a0::test::alloc(), A0_LOG_LEVEL_INFO, onmsg));

  a0_logger_t logger;
  REQUIRE_OK(a0_logger_init(&logger, topic));

  REQUIRE_OK(a0_logger_crit(&logger, a0::test::pkt("crit")));
  REQUIRE_OK(a0_logger_err(&logger, a0::test::pkt("err")));
  REQUIRE_OK(a0_logger_warn(&logger, a0::test::pkt("warn")));
  REQUIRE_OK(a0_logger_info(&logger, a0::test::pkt("info")));
  REQUIRE_OK(a0_logger_dbg(&logger, a0::test::pkt("dbg")));

  REQUIRE_OK(a0_logger_log(&logger, A0_LOG_LEVEL_CRIT, a0::test::pkt("crit")));
  REQUIRE_OK(a0_logger_log(&logger, A0_LOG_LEVEL_ERR, a0::test::pkt("err")));
  REQUIRE_OK(a0_logger_log(&logger, A0_LOG_LEVEL_WARN, a0::test::pkt("warn")));
  REQUIRE_OK(a0_logger_log(&logger, A0_LOG_LEVEL_INFO, a0::test::pkt("info")));
  REQUIRE_OK(a0_logger_log(&logger, A0_LOG_LEVEL_DBG, a0::test::pkt("dbg")));

  REQUIRE_OK(a0_logger_close(&logger));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  REQUIRE(counter == std::map<std::string, size_t>{{"CRIT", 2}, {"ERR", 2}, {"WARN", 2}, {"INFO", 2}});

  REQUIRE_OK(a0_log_listener_close(&log_list));
}
