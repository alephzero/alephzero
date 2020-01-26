#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/shm.h>

#include <doctest.h>
#include <fcntl.h>
#include <string.h>

#include <map>
#include <set>
#include <thread>
#include <vector>

#include "src/strutil.hpp"
#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";

struct PubsubFixture {
  a0_shm_t shm;

  PubsubFixture() {
    a0_shm_unlink(kTestShm);

    a0_shm_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shm_open(kTestShm, &shmopt, &shm);
  }

  ~PubsubFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(kTestShm);
  }

  a0_packet_t make_packet(std::string data) {
    a0_packet_header_t headers[1] = {{
        .key = "key",
        .val = "val",
    }};

    a0_packet_t pkt;
    REQUIRE_OK(a0_packet_build({headers, 1}, a0::test::buf(data), a0::test::allocator(), &pkt));

    return pkt;
  }
};

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub sync") {
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, shm.buf));

    REQUIRE_OK(a0_pub(&pub, make_packet("msg #0")));
    REQUIRE_OK(a0_pub(&pub, make_packet("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE_OK(a0_subscriber_sync_init(&sub,
                                       shm.buf,
                                       a0::test::allocator(),
                                       A0_INIT_OLDEST,
                                       A0_ITER_NEXT));

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));
      REQUIRE(pkt.size < 200);

      size_t num_headers;
      REQUIRE_OK(a0_packet_num_headers(pkt, &num_headers));
      REQUIRE(num_headers == 3);

      std::map<std::string, std::string> hdrs;
      for (size_t i = 0; i < num_headers; i++) {
        a0_packet_header_t pkt_hdr;
        REQUIRE_OK(a0_packet_header(pkt, i, &pkt_hdr));
        hdrs[pkt_hdr.key] = pkt_hdr.val;
      }
      REQUIRE(hdrs.count("key"));
      REQUIRE(hdrs.count("a0_id"));
      REQUIRE(hdrs.count("a0_clock"));

      a0_buf_t payload;
      REQUIRE_OK(a0_packet_payload(pkt, &payload));

      REQUIRE(a0::test::str(payload) == "msg #0");

      REQUIRE(hdrs["key"] == "val");
      REQUIRE(hdrs["a0_id"].size() == 36);
      REQUIRE(stoull(hdrs["a0_clock"]) < std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count());
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));
      REQUIRE(pkt.size < 200);

      a0_buf_t payload;
      REQUIRE_OK(a0_packet_payload(pkt, &payload));

      REQUIRE(a0::test::str(payload) == "msg #1");
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(!has_next);
    }

    REQUIRE_OK(a0_subscriber_sync_close(&sub));
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE_OK(a0_subscriber_sync_init(&sub,
                                       shm.buf,
                                       a0::test::allocator(),
                                       A0_INIT_MOST_RECENT,
                                       A0_ITER_NEWEST));

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));
      REQUIRE(pkt.size < 200);

      a0_buf_t payload;
      REQUIRE_OK(a0_packet_payload(pkt, &payload));

      REQUIRE(a0::test::str(payload) == "msg #1");
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(!has_next);
    }

    REQUIRE_OK(a0_subscriber_sync_close(&sub));
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub seek immediately await_new") {
  a0::sync<std::string> msg;

  a0_packet_callback_t cb = {
      .user_data = &msg,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (a0::sync<std::string>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([&](auto* msg_) {
              *msg_ = a0::test::str(payload);
            });
          },
  };

  a0_subscriber_t sub;
  REQUIRE_OK(a0_subscriber_init(&sub,
                                shm.buf,
                                a0::test::allocator(),
                                A0_INIT_AWAIT_NEW,
                                A0_ITER_NEXT,
                                cb));

  a0_publisher_t pub;
  REQUIRE_OK(a0_publisher_init(&pub, shm.buf));
  REQUIRE_OK(a0_pub(&pub, make_packet("msg")));
  REQUIRE_OK(a0_publisher_close(&pub));

  msg.wait([](auto* msg_) {
    return !msg_->empty();
  });

  REQUIRE(msg.copy() == "msg");
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub seek immediately most_recent") {
  a0::sync<std::string> msg;

  a0_packet_callback_t cb = {
      .user_data = &msg,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (a0::sync<std::string>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([&](auto* msg_) {
              if (msg_->empty()) {
                *msg_ = a0::test::str(payload);
              }
            });
          },
  };

  a0_publisher_t pub;
  REQUIRE_OK(a0_publisher_init(&pub, shm.buf));
  REQUIRE_OK(a0_pub(&pub, make_packet("msg before")));

  a0_subscriber_t sub;
  REQUIRE_OK(a0_subscriber_init(&sub,
                                shm.buf,
                                a0::test::allocator(),
                                A0_INIT_MOST_RECENT,
                                A0_ITER_NEXT,
                                cb));

  REQUIRE_OK(a0_pub(&pub, make_packet("msg after")));
  REQUIRE_OK(a0_publisher_close(&pub));

  msg.wait([](auto* msg_) {
    return !msg_->empty();
  });

  REQUIRE(msg.copy() == "msg before");
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub multithread") {
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, shm.buf));

    REQUIRE_OK(a0_pub(&pub, make_packet("msg #0")));
    REQUIRE_OK(a0_pub(&pub, make_packet("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  a0::sync<size_t> msg_cnt;

  a0_packet_callback_t cb = {
      .user_data = &msg_cnt,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (a0::sync<size_t>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([&](auto* msg_cnt_) {
              if (*msg_cnt_ == 0) {
                REQUIRE(a0::test::str(payload) == "msg #0");
              } else {
                REQUIRE(a0::test::str(payload) == "msg #1");
              }

              (*msg_cnt_)++;
            });
          },
  };

  a0_subscriber_t sub;
  REQUIRE_OK(
      a0_subscriber_init(&sub, shm.buf, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT, cb));

  msg_cnt.wait([](auto* msg_cnt_) {
    return (*msg_cnt_) == 2;
  });

  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub read one") {
  // TODO: Blocking, oldest, not available.
  // TODO: Blocking, most recent, not available.
  // TODO: Blocking, await new.

  // Nonblocking, oldest, not available.
  {
    a0_packet_t pkt;
    REQUIRE(
        a0_subscriber_read_one(shm.buf, a0::test::allocator(), A0_INIT_OLDEST, O_NONBLOCK, &pkt) ==
        EAGAIN);
  }

  // Nonblocking, most recent, not available.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(shm.buf,
                                   a0::test::allocator(),
                                   A0_INIT_MOST_RECENT,
                                   O_NONBLOCK,
                                   &pkt) == EAGAIN);
  }

  // Nonblocking, await new.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(shm.buf,
                                   a0::test::allocator(),
                                   A0_INIT_AWAIT_NEW,
                                   O_NONBLOCK,
                                   &pkt) == EAGAIN);
  }

  // Do writes.
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, shm.buf));

    REQUIRE_OK(a0_pub(&pub, make_packet("msg #0")));
    REQUIRE_OK(a0_pub(&pub, make_packet("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  // Blocking, oldest, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(shm.buf, a0::test::allocator(), A0_INIT_OLDEST, 0, &pkt));

    a0_buf_t payload;
    REQUIRE_OK(a0_packet_payload(pkt, &payload));
    REQUIRE(a0::test::str(payload) == "msg #0");
  }

  // Blocking, most recent, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(
        a0_subscriber_read_one(shm.buf, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &pkt));

    a0_buf_t payload;
    REQUIRE_OK(a0_packet_payload(pkt, &payload));
    REQUIRE(a0::test::str(payload) == "msg #1");
  }

  // Nonblocking, oldest, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(
        a0_subscriber_read_one(shm.buf, a0::test::allocator(), A0_INIT_OLDEST, O_NONBLOCK, &pkt));

    a0_buf_t payload;
    REQUIRE_OK(a0_packet_payload(pkt, &payload));
    REQUIRE(a0::test::str(payload) == "msg #0");
  }

  // Nonblocking, most recent, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(shm.buf,
                                      a0::test::allocator(),
                                      A0_INIT_MOST_RECENT,
                                      O_NONBLOCK,
                                      &pkt));

    a0_buf_t payload;
    REQUIRE_OK(a0_packet_payload(pkt, &payload));
    REQUIRE(a0::test::str(payload) == "msg #1");
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
      REQUIRE_OK(a0_publisher_init(&pub, shm.buf));

      for (int j = 0; j < NUM_PACKETS; j++) {
        const auto pkt = make_packet(a0::strutil::fmt("pub %d msg %d", i, j));
        REQUIRE(a0_pub(&pub, pkt) == 0);
      }

      REQUIRE_OK(a0_publisher_close(&pub));
    });
  }

  for (auto&& thread : threads) {
    thread.join();
  }

  // Now sanity-check our values.
  std::set<std::string> msgs;
  a0_subscriber_sync_t sub;
  REQUIRE_OK(
      a0_subscriber_sync_init(&sub, shm.buf, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT));

  while (true) {
    a0_packet_t pkt;
    bool has_next = false;

    REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
    if (!has_next) {
      break;
    }
    REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

    a0_buf_t payload;
    REQUIRE_OK(a0_packet_payload(pkt, &payload));

    msgs.insert(a0::test::str(payload));
  }

  REQUIRE_OK(a0_subscriber_sync_close(&sub));

  // Note that this assumes the topic is lossless.
  REQUIRE(msgs.size() == 5000);
  for (int i = 0; i < NUM_THREADS; i++) {
    for (int j = 0; j < NUM_PACKETS; j++) {
      REQUIRE(msgs.count(a0::strutil::fmt("pub %d msg %d", i, j)));
    }
  }
}
