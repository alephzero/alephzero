#include <a0/file.h>
#include <a0/latch.h>
#include <a0/log.h>
#include <a0/log.hpp>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/reader.h>

#include <doctest.h>

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

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
  struct data_t {
    std::map<std::string, size_t> cnt;
    std::mutex mu;
    a0_latch_t latch;
  } data{};
  a0_latch_init(&data.latch, 8);

  a0_packet_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            std::unique_lock<std::mutex> lk{data->mu};

            auto hdr = a0::test::hdr(pkt);
            auto range = hdr.equal_range("a0_log_level");
            for (auto it = range.first; it != range.second; ++it) {
              data->cnt[it->second]++;
              a0_latch_count_down(&data->latch, 1);
            }
          },
  };

  a0_log_listener_t log_list;
  REQUIRE_OK(a0_log_listener_init(
      &log_list,
      topic,
      a0::test::alloc(),
      A0_LOG_LEVEL_INFO,
      A0_READER_OPTIONS_DEFAULT,
      onmsg));

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

  a0_latch_wait(&data.latch);
  REQUIRE(data.cnt == std::map<std::string, size_t>{{"CRIT", 2}, {"ERR", 2}, {"WARN", 2}, {"INFO", 2}});

  REQUIRE_OK(a0_log_listener_close(&log_list));
}

TEST_CASE_FIXTURE(LogFixture, "logger] cpp basic") {
  std::map<std::string, size_t> cnt;
  std::mutex mu;
  a0_latch_t latch;
  a0_latch_init(&latch, 8);

  a0::LogListener log_listener(
      "topic",
      [&](a0::Packet pkt) {
        std::unique_lock<std::mutex> lk{mu};

        for (const auto& hdr : pkt.headers()) {
          if (hdr.first == "a0_log_level") {
            cnt[hdr.second]++;
            a0_latch_count_down(&latch, 1);
          }
        }
      });

  a0::Logger logger("topic");

  logger.crit("crit");
  logger.err("err");
  logger.warn("warn");
  logger.info("info");
  logger.dbg("dbg");

  logger.log(a0::LogLevel::CRIT, "crit");
  logger.log(a0::LogLevel::ERR, "err");
  logger.log(a0::LogLevel::WARN, "warn");
  logger.log(a0::LogLevel::INFO, "info");
  logger.log(a0::LogLevel::DBG, "dbg");

  a0_latch_wait(&latch);
  REQUIRE(cnt == std::map<std::string, size_t>{{"CRIT", 2}, {"ERR", 2}, {"WARN", 2}, {"INFO", 2}});
}
