#include <a0/common.h>
#include <a0/heartbeat.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/file_arena.h>

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <thread>

#include "src/sync.hpp"
#include "src/test_util.hpp"

static const char TEST_SHM[] = "/test.shm";

struct HeartbeatFixture {
  a0_shm_t shm;

  HeartbeatFixture() {
    a0_shm_unlink(TEST_SHM);
    a0_shm_open(TEST_SHM, nullptr, &shm);
  }

  ~HeartbeatFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(TEST_SHM);
  }
};

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hbl close, hb close") {
  a0_heartbeat_t hb;
  a0_heartbeat_options_t hb_opts{
      .freq = 100,
  };
  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(shm.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

  a0_heartbeat_listener_t hbl;
  a0_heartbeat_listener_options_t hbl_opts{
      .min_freq = 90,
  };

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

  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        shm.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_close(&hb));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hb start, hbl start, hb close, hbl close") {
  a0_heartbeat_t hb;
  a0_heartbeat_options_t hb_opts{
      .freq = 100,
  };
  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(shm.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

  a0_heartbeat_listener_t hbl;
  a0_heartbeat_listener_options_t hbl_opts{
      .min_freq = 90,
  };

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

  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        shm.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] hbl start, hb start, hb close, hbl close") {
  a0_heartbeat_listener_t hbl;
  a0_heartbeat_listener_options_t hbl_opts{
      .min_freq = 90,
  };

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

  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        shm.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  a0_heartbeat_t hb;
  a0_heartbeat_options_t hb_opts{
      .freq = 100,
  };
  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] ignore old") {
  a0_heartbeat_t hb;
  a0_heartbeat_options_t hb_opts{
      .freq = 100,
  };
  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  a0_packet_t unused;
  REQUIRE_OK(
      a0_subscriber_read_one(shm.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &unused));

  REQUIRE_OK(a0_heartbeat_close(&hb));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  // At this point, a heartbeat is written, but old.

  a0_heartbeat_listener_t hbl;
  a0_heartbeat_listener_options_t hbl_opts{
      .min_freq = 90,
  };

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

  REQUIRE_OK(a0_heartbeat_listener_init(&hbl,
                                        shm.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        onmissed));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE(detected_cnt == 0);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 50)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  REQUIRE_OK(a0_heartbeat_listener_close(&hbl));
  REQUIRE_OK(a0_heartbeat_close(&hb));
}

TEST_CASE_FIXTURE(HeartbeatFixture, "heartbeat] listener async close") {
  a0_heartbeat_t hb;
  a0_heartbeat_options_t hb_opts{
      .freq = 100,
  };
  REQUIRE_OK(a0_heartbeat_init(&hb, shm.arena, &hb_opts));

  a0_heartbeat_listener_t hbl;
  a0_heartbeat_listener_options_t hbl_opts{
      .min_freq = 90,
  };

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
                                        shm.arena,
                                        a0::test::allocator(),
                                        &hbl_opts,
                                        ondetected,
                                        A0_NONE));

  REQUIRE(data.evt.wait_for(std::chrono::nanoseconds(uint64_t(1e9 / 50))) ==
          std::cv_status::no_timeout);
  REQUIRE_OK(a0_heartbeat_close(&hb));
}
