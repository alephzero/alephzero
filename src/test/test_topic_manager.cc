#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/shmobj.h>
#include <a0/topic_manager.h>

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

static const char kJsonConfig[] = R"({
    "container": "container",
    "subscriber_maps": {
        "sub_topic": {
            "container": "container",
            "topic": "pub_topic"
        }
    }
})";

struct TopicManagerFixture {
  TopicManagerFixture() {
    a0_shmobj_unlink("/a0_pubsub__pub_container__pub_topic");
  }

  ~TopicManagerFixture() {
    a0_shmobj_unlink("/a0_pubsub__pub_container__pub_topic");
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

TEST_CASE_FIXTURE(TopicManagerFixture, "Test topic manager pubsub") {
  a0_topic_manager_t topic_manager;
  REQUIRE(a0_topic_manager_init_jsonstr(&topic_manager, kJsonConfig) == A0_OK);

  {
    a0_shmobj_t shmobj;
    REQUIRE(a0_topic_manager_publisher_topic(&topic_manager, "pub_topic", &shmobj) == A0_OK);

    a0_publisher_t pub;
    REQUIRE(a0_publisher_init(&pub, shmobj) == A0_OK);

    REQUIRE(a0_pub(&pub, make_packet("msg #0")) == A0_OK);
    REQUIRE(a0_pub(&pub, make_packet("msg #1")) == A0_OK);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);

    REQUIRE(a0_topic_manager_unref(&topic_manager, shmobj) == A0_OK);
  }

  {
    a0_shmobj_t shmobj;
    REQUIRE(a0_topic_manager_subscriber_topic(&topic_manager, "sub_topic", &shmobj) == A0_OK);

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
    REQUIRE(a0_topic_manager_unref(&topic_manager, shmobj) == A0_OK);
  }

  REQUIRE(a0_topic_manager_close(&topic_manager) == A0_OK);
}
