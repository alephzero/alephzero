#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/shmobj.h>

#include <doctest.h>
#include <string.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "src/strutil.hh"
#include "src/test_util.hh"

static const char kTestShm[] = "/test.shm";

struct PubsubFixture {
  a0_shmobj_t shmobj;

  PubsubFixture() {
    a0_shmobj_unlink(kTestShm);

    a0_shmobj_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shmobj_open(kTestShm, &shmopt, &shmobj);
  }

  ~PubsubFixture() {
    a0_shmobj_close(&shmobj);
    a0_shmobj_unlink(kTestShm);
  }

  a0_packet_t make_packet(std::string data) {
    a0_packet_header_t headers[1] = {{
        .key = "key",
        .val = "val",
    }};

    a0_packet_t pkt;
    REQUIRE(a0_packet_build(1, headers, a0::test::buf(data), a0::test::allocator(), &pkt) == A0_OK);

    return pkt;
  }
};

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub sync") {
  {
    a0_publisher_t pub;
    REQUIRE(a0_publisher_init(&pub, shmobj) == A0_OK);

    REQUIRE(a0_pub(&pub, make_packet("msg #0")) == A0_OK);
    REQUIRE(a0_pub(&pub, make_packet("msg #1")) == A0_OK);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE(a0_subscriber_sync_init(&sub,
                                    shmobj,
                                    a0::test::allocator(),
                                    A0_INIT_OLDEST,
                                    A0_ITER_NEXT) == A0_OK);

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, &pkt) == A0_OK);
      REQUIRE(pkt.size < 200);

      size_t num_headers;
      REQUIRE(a0_packet_num_headers(pkt, &num_headers) == A0_OK);
      REQUIRE(num_headers == 3);

      std::map<std::string, std::string> hdrs;
      for (size_t i = 0; i < num_headers; i++) {
        a0_packet_header_t pkt_hdr;
        REQUIRE(a0_packet_header(pkt, i, &pkt_hdr) == A0_OK);
        hdrs[pkt_hdr.key] = pkt_hdr.val;
      }
      REQUIRE(hdrs.count("key"));
      REQUIRE(hdrs.count("a0_id"));
      REQUIRE(hdrs.count("a0_send_clock"));

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(a0::test::str(payload) == "msg #0");

      REQUIRE(hdrs["key"] == "val");
      REQUIRE(hdrs["a0_id"].size() == 36);
      REQUIRE(stoull(hdrs["a0_send_clock"]) <
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count());
    }

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, &pkt) == A0_OK);
      REQUIRE(pkt.size < 200);

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(a0::test::str(payload) == "msg #1");
    }

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(!has_next);
    }

    REQUIRE(a0_subscriber_sync_close(&sub) == A0_OK);
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE(a0_subscriber_sync_init(&sub,
                                    shmobj,
                                    a0::test::allocator(),
                                    A0_INIT_MOST_RECENT,
                                    A0_ITER_NEWEST) == A0_OK);

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, &pkt) == A0_OK);
      REQUIRE(pkt.size < 200);

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(a0::test::str(payload) == "msg #1");
    }

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(!has_next);
    }

    REQUIRE(a0_subscriber_sync_close(&sub) == A0_OK);
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub multithread") {
  {
    a0_publisher_t pub;
    REQUIRE(a0_publisher_init(&pub, shmobj) == A0_OK);

    REQUIRE(a0_pub(&pub, make_packet("msg #0")) == A0_OK);
    REQUIRE(a0_pub(&pub, make_packet("msg #1")) == A0_OK);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);
  }

  {
    struct data_t {
      size_t msg_cnt{0};
      std::mutex mu;
      std::condition_variable cv;
    } data;

    a0_packet_callback_t cb = {
        .user_data = &data,
        .fn =
            [](void* vp, a0_packet_t pkt) {
              auto* data = (data_t*)vp;

              a0_buf_t payload;
              REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

              if (data->msg_cnt == 0) {
                REQUIRE(a0::test::str(payload) == "msg #0");
              }
              if (data->msg_cnt == 1) {
                REQUIRE(a0::test::str(payload) == "msg #1");
              }

              data->msg_cnt++;
              data->cv.notify_all();
            },
    };

    a0_subscriber_t sub;
    REQUIRE(
        a0_subscriber_init(&sub, shmobj, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT, cb) ==
        A0_OK);
    {
      std::unique_lock<std::mutex> lk{data.mu};
      data.cv.wait(lk, [&]() {
        return data.msg_cnt == 2;
      });
    }

    REQUIRE(a0_subscriber_await_close(&sub) == A0_OK);
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "Test close before publish") {
  // TODO(mac): DO THIS.
}

TEST_CASE_FIXTURE(PubsubFixture, "Test Pubsub many publisher fuzz") {
  constexpr int NUM_THREADS = 10;
  constexpr int NUM_PACKETS = 500;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; i++) {
    threads.emplace_back([this, i]() {
      a0_publisher_t pub;
      REQUIRE(a0_publisher_init(&pub, shmobj) == A0_OK);

      for (int j = 0; j < NUM_PACKETS; j++) {
        const auto pkt = make_packet(a0::strutil::fmt("pub %d msg %d", i, j));
        REQUIRE(a0_pub(&pub, pkt) == 0);
      }

      REQUIRE(a0_publisher_close(&pub) == A0_OK);
    });
  }

  for (auto&& thread : threads) {
    thread.join();
  }

  // Now sanity-check our values.
  std::set<std::string> msgs;
  a0_subscriber_sync_t sub;
  REQUIRE(
      a0_subscriber_sync_init(&sub, shmobj, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT) ==
      A0_OK);

  while (true) {
    a0_packet_t pkt;
    bool has_next = false;

    REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
    if (!has_next) {
      break;
    }
    REQUIRE(a0_subscriber_sync_next(&sub, &pkt) == A0_OK);

    a0_buf_t payload;
    REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

    msgs.insert(a0::test::str(payload));
  }

  REQUIRE(a0_subscriber_sync_close(&sub) == A0_OK);

  // Note that this assumes the topic is lossless.
  REQUIRE(msgs.size() == 5000);
  for (int i = 0; i < NUM_THREADS; i++) {
    for (int j = 0; j < NUM_PACKETS; j++) {
      REQUIRE(msgs.count(a0::strutil::fmt("pub %d msg %d", i, j)));
    }
  }
}
