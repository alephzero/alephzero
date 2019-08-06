#include <a0/pubsub.h>

#include <a0/internal/strutil.hh>
#include <a0/internal/test_util.hh>

#include <doctest.h>

#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

struct PubsubFixture {
  std::string topic_name = "topic";
  std::string container_name = "container";

  PubsubFixture() {
    a0_shmobj_unlink("/a0_pubsub__container__topic");
  }

  ~PubsubFixture() {
    a0_shmobj_unlink("/a0_pubsub__container__topic");
  }

  a0_packet_t malloc_packet(std::string data) {
    a0_packet_builder_t builder;
    builder.num_headers = 0;
    builder.payload.size = data.size();
    builder.payload.ptr = (uint8_t*)data.c_str();

    a0_packet_t pkt;
    REQUIRE(a0_packet_build(builder, make_alloc(), &pkt) == A0_OK);

    return pkt;
  }

  void free_packet(a0_packet_t pkt) {
    free(pkt.ptr);
  }

  a0_alloc_t make_alloc() {
    a0_alloc_t alloc;
    alloc.fn = [](void*, size_t size, a0_buf_t* buf) {
      buf->size = size;
      buf->ptr = (uint8_t*)malloc(size);
    };
    return alloc;
  }
};

TEST_CASE_FIXTURE(PubsubFixture, "Test pubsub sync") {
  a0_topic_t topic;
  topic.name.ptr = (uint8_t*)topic_name.c_str();
  topic.name.size = topic_name.size();
  topic.container.ptr = (uint8_t*)container_name.c_str();
  topic.container.size = container_name.size();

  {
    a0_publisher_t pub;
    REQUIRE(a0_publisher_init(&pub, topic) == A0_OK);

    a0_packet_t pkt = malloc_packet(a0::strutil::cat("msg #", 0));
    REQUIRE(a0_pub(&pub, pkt) == A0_OK);
    free_packet(pkt);

    pkt = malloc_packet(a0::strutil::cat("msg #", 1));
    REQUIRE(a0_pub(&pub, pkt) == A0_OK);
    free_packet(pkt);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);
  }

  {
    a0_subscriber_sync_t sub;
    REQUIRE(a0_subscriber_sync_open(&sub, topic, A0_READ_START_EARLIEST, A0_READ_NEXT_SEQUENTIAL) ==
            A0_OK);

    uint8_t space[100];
    a0_alloc_t alloc;
    alloc.user_data = space;
    alloc.fn = [](void* data, size_t size, a0_packet_t* pkt) {
      pkt->size = size;
      pkt->ptr = (uint8_t*)data;
    };

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, alloc, &pkt) == A0_OK);
      REQUIRE(pkt.size == 22);

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(str(payload) == "msg #0");
    }

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, alloc, &pkt) == A0_OK);
      REQUIRE(pkt.size == 22);

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(str(payload) == "msg #1");
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
    REQUIRE(a0_subscriber_sync_open(&sub, topic, A0_READ_START_LATEST, A0_READ_NEXT_RECENT) ==
            A0_OK);

    uint8_t space[22];
    a0_alloc_t alloc;
    alloc.user_data = space;
    alloc.fn = [](void* data, size_t size, a0_packet_t* pkt) {
      pkt->size = size;
      pkt->ptr = (uint8_t*)data;
    };

    {
      bool has_next;
      REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
      REQUIRE(has_next);

      a0_packet_t pkt;
      REQUIRE(a0_subscriber_sync_next(&sub, alloc, &pkt) == A0_OK);
      REQUIRE(pkt.size == 22);

      a0_buf_t payload;
      REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

      REQUIRE(str(payload) == "msg #1");
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
  a0_topic_t topic;
  topic.name.ptr = (uint8_t*)topic_name.c_str();
  topic.name.size = topic_name.size();
  topic.container.ptr = (uint8_t*)container_name.c_str();
  topic.container.size = container_name.size();

  {
    a0_publisher_t pub;
    REQUIRE(a0_publisher_init(&pub, topic) == A0_OK);

    a0_packet_t pkt = malloc_packet(a0::strutil::cat("msg #", 0));
    REQUIRE(a0_pub(&pub, pkt) == A0_OK);
    free_packet(pkt);

    pkt = malloc_packet(a0::strutil::cat("msg #", 1));
    REQUIRE(a0_pub(&pub, pkt) == A0_OK);
    free_packet(pkt);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);
  }

  {
    uint8_t space[100];
    a0_alloc_t alloc = {
        .user_data = space,
        .fn =
            [](void* data, size_t size, a0_packet_t* pkt) {
              pkt->size = size;
              pkt->ptr = (uint8_t*)data;
            },
    };

    struct data_t {
      size_t msg_cnt{0};
      bool closed{false};
      std::mutex mu;
      std::condition_variable cv;
    } data;

    a0_subscriber_callback_t cb = {
        .user_data = &data,
        .fn =
            [](void* vp, a0_packet_t pkt) {
              auto* data = (data_t*)vp;

              a0_buf_t payload;
              REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

              if (data->msg_cnt == 0) {
                REQUIRE(str(payload) == "msg #0");
              }
              if (data->msg_cnt == 1) {
                REQUIRE(str(payload) == "msg #1");
              }

              data->msg_cnt++;
              data->cv.notify_all();
            },
    };

    a0_subscriber_t sub;
    REQUIRE(a0_subscriber_open(&sub,
                               topic,
                               A0_READ_START_EARLIEST,
                               A0_READ_NEXT_SEQUENTIAL,
                               alloc,
                               cb) == A0_OK);
    {
      std::unique_lock<std::mutex> lk{data.mu};
      data.cv.wait(lk, [&]() {
        return data.msg_cnt == 2;
      });
    }

    a0_callback_t close_cb = {
        .user_data = &data,
        .fn =
            [](void* vp) {
              auto* data = (data_t*)vp;
              data->closed = true;
              data->cv.notify_all();
            },
    };

    REQUIRE(a0_subscriber_close(&sub, close_cb) == A0_OK);
    {
      std::unique_lock<std::mutex> lk{data.mu};
      data.cv.wait(lk, [&]() {
        return data.closed;
      });
    }
  }
}

TEST_CASE_FIXTURE(PubsubFixture, "Test close before publish") {
  // TODO(mac) DO THIS.
}

TEST_CASE_FIXTURE(PubsubFixture, "Test Pubsub many publisher fuzz") {
  a0_topic_t topic;
  topic.name.ptr = (uint8_t*)topic_name.c_str();
  topic.name.size = topic_name.size();
  topic.container.ptr = (uint8_t*)container_name.c_str();
  topic.container.size = container_name.size();

  constexpr int NUM_THREADS = 10;
  constexpr int NUM_PACKETS = 500;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; i++) {
    threads.emplace_back([i, &topic, this]() {
      a0_publisher_t pub;
      REQUIRE(a0_publisher_init(&pub, topic) == 0);

      for (int j = 0; j < NUM_PACKETS; j++) {
        const auto pkt = malloc_packet(a0::strutil::cat("pub ", i, " msg ", j));
        REQUIRE(a0_pub(&pub, pkt) == 0);
        free_packet(pkt);
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
  REQUIRE(a0_subscriber_sync_open(&sub, topic, A0_READ_START_EARLIEST, A0_READ_NEXT_SEQUENTIAL) ==
          A0_OK);

  uint8_t space[100];
  a0_alloc_t alloc = {
      .user_data = space,
      .fn =
          [](void* data, size_t size, a0_packet_t* pkt) {
            pkt->size = size;
            pkt->ptr = (uint8_t*)data;
          },
  };

  while (true) {
    a0_packet_t pkt;
    bool has_next = false;

    REQUIRE(a0_subscriber_sync_has_next(&sub, &has_next) == A0_OK);
    if (!has_next) {
      break;
    }
    REQUIRE(a0_subscriber_sync_next(&sub, alloc, &pkt) == A0_OK);

    a0_buf_t payload;
    REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);

    msgs.insert(str(payload));
  }

  // Note that this assumes the topic is lossless.
  REQUIRE(msgs.size() == 5000);
  for (int i = 0; i < NUM_THREADS; i++) {
    for (int j = 0; j < NUM_PACKETS; j++) {
      REQUIRE(msgs.find(a0::strutil::cat("pub ", i, " msg ", j)) != msgs.end());
    }
  }
}
