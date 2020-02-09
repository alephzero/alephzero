#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <doctest.h>
#include <string.h>

#include <map>
#include <set>
#include <thread>
#include <vector>

#include "src/strutil.hpp"
#include "src/test_util.hpp"

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
    a0_shm_unlink("/a0_pubsub__container__pub_topic");
  }

  ~TopicManagerFixture() {
    a0_shm_unlink("/a0_pubsub__container__pub_topic");
  }

  a0_packet_t make_packet(std::string data) {
    a0_packet_header_t hdr = {"key", "val"};

    a0_packet_t pkt;
    REQUIRE_OK(a0_packet_build({a0::test::header_block(&hdr), a0::test::buf(data)},
                               a0::test::allocator(),
                               &pkt));

    return pkt;
  }
};

TEST_CASE_FIXTURE(TopicManagerFixture, "Test topic manager pubsub") {
  a0_topic_manager_t topic_manager;
  REQUIRE_OK(a0_topic_manager_init(&topic_manager, kJsonConfig));

  {
    a0_shm_t shm;
    REQUIRE_OK(a0_topic_manager_open_publisher_topic(&topic_manager, "pub_topic", &shm));

    a0_publisher_t pub;
    REQUIRE_OK(a0_publisher_init(&pub, shm.buf));

    REQUIRE_OK(a0_pub(&pub, make_packet("msg #0")));
    REQUIRE_OK(a0_pub(&pub, make_packet("msg #1")));

    REQUIRE_OK(a0_publisher_close(&pub));

    REQUIRE_OK(a0_shm_close(&shm));
  }

  {
    a0_shm_t shm;
    REQUIRE_OK(a0_topic_manager_open_subscriber_topic(&topic_manager, "sub_topic", &shm));

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
      REQUIRE(pkt.size < 300);

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
    REQUIRE_OK(a0_shm_close(&shm));
  }

  REQUIRE_OK(a0_topic_manager_close(&topic_manager));
}
