#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/pubsub.hpp>
#include <a0/reader.h>
#include <a0/string_view.hpp>

#include <doctest.h>
#include <fcntl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/test_util.hpp"

struct PubsubFixture {
  a0_pubsub_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.pubsub.a0";

  PubsubFixture() {
    a0_file_remove(topic_path);
  }

  ~PubsubFixture() {
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] sync") {
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, topic));

    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt({{"key0", "val0"}, {"key1", "val1"}}, "msg #0")));
    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt({{"key2", "val2"}}, "msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, topic));

    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt({{"key3", "val3"}}, "msg #2")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE_OK(a0_subscriber_sync_init(&sub,
                                       topic,
                                       a0::test::alloc(),
                                       A0_INIT_OLDEST,
                                       A0_ITER_NEXT));

    uint64_t pkt1_time_mono;

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

      REQUIRE(pkt.headers_block.size == 7);
      REQUIRE(pkt.headers_block.next_block == nullptr);

      std::map<std::string, std::string> hdrs;
      for (size_t i = 0; i < pkt.headers_block.size; i++) {
        auto hdr = pkt.headers_block.headers[i];
        hdrs[hdr.key] = hdr.val;
      }

      REQUIRE(a0::test::str(pkt.payload) == "msg #0");

      REQUIRE(hdrs["key0"] == "val0");
      REQUIRE(hdrs["key1"] == "val1");
      REQUIRE(hdrs["a0_time_mono"].size() == 19);
      REQUIRE(hdrs["a0_time_wall"].size() == 35);
      REQUIRE(hdrs["a0_transport_seq"] == "0");
      REQUIRE(hdrs["a0_writer_seq"] == "0");
      REQUIRE(hdrs["a0_writer_id"].size() == 36);
      pkt1_time_mono = stoull(hdrs["a0_time_mono"]);
      REQUIRE(pkt1_time_mono > 0);
      REQUIRE(pkt1_time_mono < UINT64_MAX);
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

      std::map<std::string, std::string> hdrs;
      for (size_t i = 0; i < pkt.headers_block.size; i++) {
        auto hdr = pkt.headers_block.headers[i];
        hdrs[hdr.key] = hdr.val;
      }

      REQUIRE(a0::test::str(pkt.payload) == "msg #1");

      REQUIRE(hdrs["key2"] == "val2");
      REQUIRE(hdrs["a0_time_mono"].size() == 19);
      REQUIRE(hdrs["a0_time_wall"].size() == 35);
      REQUIRE(hdrs["a0_transport_seq"] == "1");
      REQUIRE(hdrs["a0_writer_seq"] == "1");
      REQUIRE(hdrs["a0_writer_id"].size() == 36);

      uint64_t pkt2_time_mono = stoull(hdrs["a0_time_mono"]);
      REQUIRE(pkt2_time_mono > pkt1_time_mono);
      REQUIRE(pkt2_time_mono < UINT64_MAX);
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

      std::map<std::string, std::string> hdrs;
      for (size_t i = 0; i < pkt.headers_block.size; i++) {
        auto hdr = pkt.headers_block.headers[i];
        hdrs[hdr.key] = hdr.val;
      }

      REQUIRE(a0::test::str(pkt.payload) == "msg #2");

      REQUIRE(hdrs["key3"] == "val3");
      REQUIRE(hdrs["a0_time_mono"].size() == 19);
      REQUIRE(hdrs["a0_time_wall"].size() == 35);
      REQUIRE(hdrs["a0_transport_seq"] == "2");
      REQUIRE(hdrs["a0_writer_seq"] == "0");
      REQUIRE(hdrs["a0_writer_id"].size() == 36);
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
                                       topic,
                                       a0::test::alloc(),
                                       A0_INIT_MOST_RECENT,
                                       A0_ITER_NEWEST));

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));
      REQUIRE(a0::test::str(pkt.payload) == "msg #2");
    }

    {
      bool has_next;
      REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
      REQUIRE(!has_next);
    }

    REQUIRE_OK(a0_subscriber_sync_close(&sub));
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] cpp sync") {
  {
    a0::Publisher p(topic.name);

    p.pub(a0::Packet({{"key0", "val0"}, {"key1", "val1"}}, "msg #0"));
    p.pub(a0::Packet({{"key2", "val2"}}, "msg #1"));
  }
  {
    a0::Publisher p(topic.name);
    p.pub(a0::Packet({{"key3", "val3"}}, "msg #2"));
  }

  {
    a0::SubscriberSync sub(topic.name, A0_INIT_OLDEST, A0_ITER_NEXT);

    uint64_t pkt1_time_mono;

    {
      REQUIRE(sub.has_next());
      auto pkt = sub.next();

      REQUIRE(pkt.headers().size() == 7);
      REQUIRE(pkt.payload() == "msg #0");

      auto hdrs = pkt.headers();
      REQUIRE(hdrs.find("key0")->second == "val0");
      REQUIRE(hdrs.find("key1")->second == "val1");
      REQUIRE(hdrs.find("a0_time_mono")->second.size() == 19);
      REQUIRE(hdrs.find("a0_time_wall")->second.size() == 35);
      REQUIRE(hdrs.find("a0_transport_seq")->second == "0");
      REQUIRE(hdrs.find("a0_writer_seq")->second == "0");
      REQUIRE(hdrs.find("a0_writer_id")->second.size() == 36);
      pkt1_time_mono = stoull(hdrs.find("a0_time_mono")->second);
      REQUIRE(pkt1_time_mono > 0);
      REQUIRE(pkt1_time_mono < UINT64_MAX);
    }

    {
      REQUIRE(sub.has_next());
      auto pkt = sub.next();

      REQUIRE(pkt.headers().size() == 6);
      REQUIRE(pkt.payload() == "msg #1");

      auto hdrs = pkt.headers();
      REQUIRE(hdrs.find("key2")->second == "val2");
      REQUIRE(hdrs.find("a0_time_mono")->second.size() == 19);
      REQUIRE(hdrs.find("a0_time_wall")->second.size() == 35);
      REQUIRE(hdrs.find("a0_transport_seq")->second == "1");
      REQUIRE(hdrs.find("a0_writer_seq")->second == "1");
      REQUIRE(hdrs.find("a0_writer_id")->second.size() == 36);

      uint64_t pkt2_time_mono = stoull(hdrs.find("a0_time_mono")->second);
      REQUIRE(pkt2_time_mono > pkt1_time_mono);
      REQUIRE(pkt2_time_mono < UINT64_MAX);
    }

    {
      REQUIRE(sub.has_next());
      auto pkt = sub.next();

      REQUIRE(pkt.headers().size() == 6);
      REQUIRE(pkt.payload() == "msg #2");

      auto hdrs = pkt.headers();
      REQUIRE(hdrs.find("key3")->second == "val3");
      REQUIRE(hdrs.find("a0_time_mono")->second.size() == 19);
      REQUIRE(hdrs.find("a0_time_wall")->second.size() == 35);
      REQUIRE(hdrs.find("a0_transport_seq")->second == "2");
      REQUIRE(hdrs.find("a0_writer_seq")->second == "0");
      REQUIRE(hdrs.find("a0_writer_id")->second.size() == 36);
    }

    REQUIRE(!sub.has_next());
  }

  {
    a0::SubscriberSync sub(topic.name, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

    REQUIRE(sub.has_next());
    auto pkt = sub.next();
    REQUIRE(pkt.payload() == "msg #2");

    REQUIRE(!sub.has_next());
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] seek immediately await_new") {
  struct data_t {
    std::string msg;
    a0::test::Latch latch{1};
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            data->msg = a0::test::str(pkt.payload);
            data->latch.count_down();
          },
  };

  a0_publisher_t pub;
  REQUIRE_OK(a0_publisher_init(&pub, topic));
  REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg before")));

  a0_subscriber_t sub;
  REQUIRE_OK(a0_subscriber_init(&sub,
                                topic,
                                a0::test::alloc(),
                                A0_INIT_AWAIT_NEW,
                                A0_ITER_NEXT,
                                cb));

  REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg after")));
  REQUIRE_OK(a0_publisher_close(&pub));

  data.latch.wait();

  REQUIRE(data.msg == "msg after");
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] seek immediately most_recent") {
  struct data_t {
    std::string msg;
    a0::test::Latch latch{1};
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            if (data->msg.empty()) {
              data->msg = a0::test::str(pkt.payload);
              data->latch.count_down();
            }
          },
  };

  a0_publisher_t pub;
  REQUIRE_OK(a0_publisher_init(&pub, topic));
  REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg before")));

  a0_subscriber_t sub;
  REQUIRE_OK(a0_subscriber_init(&sub,
                                topic,
                                a0::test::alloc(),
                                A0_INIT_MOST_RECENT,
                                A0_ITER_NEXT,
                                cb));

  REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg after")));
  REQUIRE_OK(a0_publisher_close(&pub));

  data.latch.wait();

  REQUIRE(data.msg == "msg before");
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] multithread") {
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, topic));

    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg #0")));
    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  struct data_t {
    size_t msg_cnt;
    a0::test::Latch latch{2};
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;

            if (!data->msg_cnt) {
              REQUIRE(a0::test::str(pkt.payload) == "msg #0");
            } else {
              REQUIRE(a0::test::str(pkt.payload) == "msg #1");
            }

            data->msg_cnt++;
            data->latch.count_down();
          },
  };

  a0_subscriber_t sub;
  REQUIRE_OK(a0_subscriber_init(&sub,
                                topic,
                                a0::test::alloc(),
                                A0_INIT_OLDEST,
                                A0_ITER_NEXT,
                                cb));

  data.latch.wait();

  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] read one") {
  // TODO: Blocking, oldest, not available.
  // TODO: Blocking, most recent, not available.
  // TODO: Blocking, await new.

  // Nonblocking, oldest, not available.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(topic,
                                   a0::test::alloc(),
                                   A0_INIT_OLDEST,
                                   O_NONBLOCK,
                                   &pkt) == A0_ERR_AGAIN);
  }

  // Nonblocking, most recent, not available.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(topic,
                                   a0::test::alloc(),
                                   A0_INIT_MOST_RECENT,
                                   O_NONBLOCK,
                                   &pkt) == A0_ERR_AGAIN);
  }

  // Nonblocking, await new.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(topic,
                                   a0::test::alloc(),
                                   A0_INIT_AWAIT_NEW,
                                   O_NONBLOCK,
                                   &pkt) == A0_ERR_AGAIN);
  }

  // Do writes.
  {
    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, topic));

    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg #0")));
    REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));
  }

  // Blocking, oldest, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(topic,
                                      a0::test::alloc(),
                                      A0_INIT_OLDEST,
                                      0,
                                      &pkt));
    REQUIRE(a0::test::str(pkt.payload) == "msg #0");
  }

  // Blocking, most recent, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(topic,
                                      a0::test::alloc(),
                                      A0_INIT_MOST_RECENT,
                                      0,
                                      &pkt));
    REQUIRE(a0::test::str(pkt.payload) == "msg #1");
  }

  // Nonblocking, oldest, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(topic,
                                      a0::test::alloc(),
                                      A0_INIT_OLDEST,
                                      O_NONBLOCK,
                                      &pkt));
    REQUIRE(a0::test::str(pkt.payload) == "msg #0");
  }

  // Nonblocking, most recent, available.
  {
    a0_packet_t pkt;
    REQUIRE_OK(a0_subscriber_read_one(topic,
                                      a0::test::alloc(),
                                      A0_INIT_MOST_RECENT,
                                      O_NONBLOCK,
                                      &pkt));
    REQUIRE(a0::test::str(pkt.payload) == "msg #1");
  }

  // Nonblocking, await new.
  {
    a0_packet_t pkt;
    REQUIRE(a0_subscriber_read_one(topic,
                                   a0::test::alloc(),
                                   A0_INIT_AWAIT_NEW,
                                   O_NONBLOCK,
                                   &pkt) == A0_ERR_AGAIN);
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] many publisher fuzz") {
  constexpr int NUM_THREADS = 10;
  constexpr int NUM_PACKETS = 500;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; i++) {
    threads.emplace_back([this, i]() {
      a0_publisher_t pub;
      REQUIRE_OK(a0_publisher_init(&pub, topic));

      for (int j = 0; j < NUM_PACKETS; j++) {
        REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt(a0::test::fmt("pub %d msg %d", i, j))));
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
  REQUIRE_OK(a0_subscriber_sync_init(&sub,
                                     topic,
                                     a0::test::alloc(),
                                     A0_INIT_OLDEST,
                                     A0_ITER_NEXT));

  while (true) {
    a0_packet_t pkt;
    bool has_next = false;

    REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
    if (!has_next) {
      break;
    }
    REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

    msgs.insert(a0::test::str(pkt.payload));
  }

  REQUIRE_OK(a0_subscriber_sync_close(&sub));

  // Note that this assumes the topic is lossless.
  REQUIRE(msgs.size() == NUM_THREADS * NUM_PACKETS);
  for (int i = 0; i < NUM_THREADS; i++) {
    for (int j = 0; j < NUM_PACKETS; j++) {
      REQUIRE(msgs.count(a0::test::fmt("pub %d msg %d", i, j)));
    }
  }
}
