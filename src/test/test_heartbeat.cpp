#include <a0/callback.h>
#include <a0/file.h>
#include <a0/heartbeat.h>
#include <a0/packet.h>
#include <a0/reader.h>

#include <doctest.h>

#include <chrono>
#include <cstdint>
#include <thread>

#include "src/test_util.hpp"

struct HeartbeatFixture {
  a0_heartbeat_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.heartbeat.a0";

  a0_heartbeat_options_t hb_opts;
  a0_heartbeat_listener_options_t hbl_opts;
  std::chrono::nanoseconds sync_duration;

  HeartbeatFixture() {
    a0_file_remove(topic_path);

    hb_opts.freq = 100;
    if (a0::test::is_debug_mode()) {
      hbl_opts.min_freq = 25;
      sync_duration = std::chrono::nanoseconds(uint64_t(1e9 / 10));
    } else {
      hbl_opts.min_freq = 80;
      sync_duration = std::chrono::nanoseconds(uint64_t(1e9 / 40));
    }
  }

  ~HeartbeatFixture() {
    a0_file_remove(topic_path);
  }

  void wait_heartbeat() {
    a0_file_t file;
    a0_file_open(topic_path, nullptr, &file);
    a0_packet_t unused;
    REQUIRE_OK(
        a0_reader_read_one(file.arena, a0::test::alloc(), A0_INIT_MOST_RECENT, 0, &unused));
    a0_file_close(&file);
  }
};

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hbl close, hb close") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, topic, &hb_opts));

  wait_heartbeat();

  int detected_cnt = 0;
  int missed_cnt = 0;

  a0_callback_t ondetected = {
      .user_data = &detected_cnt,
      .fn =
          [](void* user_data) {
            (*(int*)user_data)++;
          },
  };
  a0_callback_t onmissed = {
      .user_data = &missed_cnt,
      .fn =
          [](void* user_data) {
            (*(int*)user_data)++;
          },
  };

  a0_heartbeat_listener_t hbl;
  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        topic,
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_close(&hb));
}

#if 0

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hb close, hbl close") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, topic, &hb_opts));

  wait_heartbeat();

  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  a0_callback_t ondetected = {
      .user_data = &detected_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };
  a0_callback_t onmissed = {
      .user_data = &missed_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };

  a0_heartbeat_listener_t hbl;
  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        topic,
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hbl start, hb start, hb close, hbl close") {
  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  a0_callback_t ondetected = {
      .user_data = &detected_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };
  a0_callback_t onmissed = {
      .user_data = &missed_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };

  a0_heartbeat_listener_t hbl;
  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        topic,
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, topic, &hb_opts));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] ignore old") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, topic, &hb_opts));

  wait_heartbeat();

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(sync_duration);

  // At this point, a heartbeat is written, but old.

  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  a0_callback_t ondetected = {
      .user_data = &detected_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };
  a0_callback_t onmissed = {
      .user_data = &missed_cnt,
      .fn =
          [](void* user_data) {
            (*(std::atomic<int>*)user_data)++;
          },
  };

  a0_heartbeat_listener_t hbl;
  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        topic,
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 0);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_init(&hb, topic, &hb_opts));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
  REQUIRE_OK(a0_heartbeat_close(&hb));
}

#endif