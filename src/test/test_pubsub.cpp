#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/pubsub.hpp>
#include <a0/reader.h>
#include <a0/string_view.hpp>
#include <a0/time.hpp>

#include <doctest.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
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
  const char* topic_path = "test.pubsub.a0";
  std::vector<std::thread> threads;

  PubsubFixture() {
    a0_file_remove(topic_path);
  }

  ~PubsubFixture() {
    a0_file_remove(topic_path);
  }

  void thread_sleep_push_pkt(std::chrono::nanoseconds timeout, a0::Packet pkt) {
    threads.emplace_back([this, timeout, pkt]() {
      std::this_thread::sleep_for(timeout);
      a0::Publisher(topic.name).pub(pkt);
    });
  }

  void join_threads() {
    for (auto&& t : threads) {
      t.join();
    }
    threads.clear();
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
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(can_read);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_read(&sub, &pkt));

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
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(can_read);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_read(&sub, &pkt));

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
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(can_read);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_read(&sub, &pkt));

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
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(!can_read);
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
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(can_read);

      a0_packet_t pkt;
      REQUIRE_OK(a0_subscriber_sync_read(&sub, &pkt));
      REQUIRE(a0::test::str(pkt.payload) == "msg #2");
    }

    {
      bool can_read;
      REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
      REQUIRE(!can_read);
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
      REQUIRE(sub.can_read());
      auto pkt = sub.read();

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
      REQUIRE(sub.can_read());
      auto pkt = sub.read();

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
      REQUIRE(sub.can_read());
      auto pkt = sub.read();

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

    REQUIRE(!sub.can_read());
  }

  {
    a0::SubscriberSync sub(topic.name, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

    REQUIRE(sub.can_read());
    auto pkt = sub.read();
    REQUIRE(pkt.payload() == "msg #2");

    REQUIRE(!sub.can_read());
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] await_new") {
  struct data_t {
    std::vector<std::string> msgs;
    a0::test::Latch latch{1};
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            data->msgs.push_back(a0::test::str(pkt.payload));
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

  REQUIRE(data.msgs == std::vector<std::string>{"msg after"});
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] cpp await_new") {
  std::vector<std::string> msgs;
  a0::test::Latch latch{1};

  a0::Publisher p(topic.name);
  p.pub("msg before");

  a0::Subscriber sub(
      topic.name,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      [&](a0::Packet pkt) {
        msgs.push_back(std::string(pkt.payload()));
        latch.count_down();
      });

  p.pub("msg after");

  latch.wait();

  REQUIRE(msgs == std::vector<std::string>{"msg after"});
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] most_recent") {
  struct data_t {
    std::vector<std::string> msgs;
    a0::test::Latch latch{2};
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            data->msgs.push_back(a0::test::str(pkt.payload));
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
                                A0_INIT_MOST_RECENT,
                                A0_ITER_NEXT,
                                cb));

  REQUIRE_OK(a0_publisher_pub(&pub, a0::test::pkt("msg after")));
  REQUIRE_OK(a0_publisher_close(&pub));

  data.latch.wait();

  REQUIRE(data.msgs == std::vector<std::string>{"msg before", "msg after"});
  REQUIRE_OK(a0_subscriber_close(&sub));
}

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] cpp most_recent") {
  std::vector<std::string> msgs;
  a0::test::Latch latch{2};

  a0::Publisher p(topic.name);
  p.pub("msg before");

  a0::Subscriber sub(
      topic.name,
      A0_INIT_MOST_RECENT,
      A0_ITER_NEXT,
      [&](a0::Packet pkt) {
        msgs.push_back(std::string(pkt.payload()));
        latch.count_down();
      });

  p.pub("msg after");

  latch.wait();

  REQUIRE(msgs == std::vector<std::string>{"msg before", "msg after"});
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

TEST_CASE_FIXTURE(PubsubFixture, "pubsub] cpp sync blocking") {
  // Nonblocking, oldest, not available.
  REQUIRE_THROWS_WITH(
      [&]() { a0::SubscriberSync(topic.name, A0_INIT_OLDEST, A0_ITER_NEXT).read(); }(),
      "Not available yet");

  // Nonblocking, most recent, not available.
  REQUIRE_THROWS_WITH(
      [&]() { a0::SubscriberSync(topic.name, A0_INIT_MOST_RECENT, A0_ITER_NEXT).read(); }(),
      "Not available yet");

  // Nonblocking, await new.
  REQUIRE_THROWS_WITH(
      [&]() { a0::SubscriberSync(topic.name, A0_INIT_AWAIT_NEW, A0_ITER_NEXT).read(); }(),
      "Not available yet");

  // Do writes.
  thread_sleep_push_pkt(std::chrono::milliseconds(1), a0::Packet("msg #0"));

  // Blocking, oldest.
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_OLDEST, A0_ITER_NEXT).read_blocking().payload() == "msg #0");
  join_threads();

  a0::Publisher(topic.name).pub("msg #1");

  // Blocking, most recent, available.
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_MOST_RECENT, A0_ITER_NEXT).read_blocking().payload() == "msg #1");

  // Nonblocking, oldest, available.
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_OLDEST, A0_ITER_NEXT).read().payload() == "msg #0");

  // Nonblocking, most recent, available.
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_MOST_RECENT, A0_ITER_NEXT).read().payload() == "msg #1");

  // Blocking, await new, must wait.
  thread_sleep_push_pkt(std::chrono::milliseconds(1), a0::Packet("msg #2"));
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_AWAIT_NEW, A0_ITER_NEXT).read_blocking().payload() == "msg #2");
  join_threads();

  // Blocking, await new, must wait, sufficient timeout.
  thread_sleep_push_pkt(std::chrono::milliseconds(1), a0::Packet("msg #3"));
  auto block_timeout = a0::TimeMono::now() + std::chrono::milliseconds(5);
  REQUIRE(a0::SubscriberSync(topic.name, A0_INIT_AWAIT_NEW, A0_ITER_NEXT).read_blocking(block_timeout).payload() == "msg #3");
  join_threads();

  // Blocking, await new, must wait, insufficient timeout.
  thread_sleep_push_pkt(std::chrono::milliseconds(5), a0::Packet("msg #4"));
  block_timeout = a0::TimeMono::now() + std::chrono::milliseconds(1);
  REQUIRE_THROWS_WITH(
      [&]() { a0::SubscriberSync(topic.name, A0_INIT_AWAIT_NEW, A0_ITER_NEXT).read_blocking(block_timeout); }(),
      strerror(ETIMEDOUT));
  join_threads();

  // Nonblocking, await new.
  REQUIRE_THROWS_WITH(
      [&]() { a0::SubscriberSync(topic.name, A0_INIT_AWAIT_NEW, A0_ITER_NEXT).read(); }(),
      "Not available yet");
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
    bool can_read = false;

    REQUIRE_OK(a0_subscriber_sync_can_read(&sub, &can_read));
    if (!can_read) {
      break;
    }
    REQUIRE_OK(a0_subscriber_sync_read(&sub, &pkt));

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
