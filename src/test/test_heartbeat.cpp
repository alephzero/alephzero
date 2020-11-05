#include <a0/arena.h>
#include <a0/common.h>
#include <a0/heartbeat.h>
#include <a0/packet.h>
#include <a0/pubsub.h>

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <thread>

#include "src/sync.hpp"
#include "src/test_util.hpp"

static const char TEST_FILE[] = "test.file";

struct HeartbeatFixture {
  a0_file_t file;
  a0_heartbeat_options_t hb_opts;
  a0_heartbeat_listener_options_t hbl_opts;
  std::chrono::nanoseconds sync_duration;

  HeartbeatFixture() {
    a0_file_remove(TEST_FILE);
    a0_file_open(TEST_FILE, nullptr, &file);

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
    a0_file_close(&file);
    a0_file_remove(TEST_FILE);
  }
};

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hbl close, hb close") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

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
                                        file.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_close(&hb));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hb close, hbl close") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

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
                                        file.arena,
                                        a0::test::allocator(),
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
                                        file.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

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
  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

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
                                        file.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 0);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

  std::this_thread::sleep_for(sync_duration);

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
  REQUIRE_OK(a0_heartbeat_close(&hb));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] listener async close") {
  a0_heartbeat_t hb;
  REQUIRE_OK(a0_heartbeat_init(&hb, file.arena, &hb_opts));

  a0_heartbeat_listener_t hbl;

  struct data_t {
    a0_heartbeat_listener_t* hbl;
    a0::Event evt;
  } data{&hbl, {}};

  a0_callback_t ondetected = {
      .user_data = &data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;

            a0_callback_t onclose = {
                .user_data = &data->evt,
                .fn =
                    [](void* user_data) {
                      ((a0::Event*)user_data)->set();
                    },
            };

            a0_heartbeat_listener_async_close(data->hbl, onclose);
          },
  };

  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        file.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        {}));

  REQUIRE(data.evt.wait_for(sync_duration) ==
          std::cv_status::no_timeout);
  REQUIRE_OK(a0_heartbeat_close(&hb));
}
