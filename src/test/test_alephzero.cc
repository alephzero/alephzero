#include <a0/alephzero.h>
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

struct AlephZeroFixture {
  AlephZeroFixture() {
    REQUIRE(setenv("A0_CFG", R"({
        "container": "sub_container",
        "subscriber_maps": {
            "sub_topic": {
                "container": "pub_container",
                "topic": "pub_topic"
            }
        }
    })", 1) == 0);
    a0_shmobj_unlink("/a0_pubsub__pub_container__pub_topic");
  }

  ~AlephZeroFixture() {
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

TEST_CASE_FIXTURE(AlephZeroFixture, "Test alephzero pubsub") {
  {
    a0_shmobj_t shmobj;
    a0_shmobj_options_t shmopt = {
        .size = 16 * 1024 * 1024,
    };
    REQUIRE(a0_shmobj_open("/a0_pubsub__pub_container__pub_topic", &shmopt, &shmobj) == A0_OK);

    a0_publisher_t pub;
    REQUIRE(a0_publisher_init_unmanaged(&pub, shmobj) == A0_OK);

    REQUIRE(a0_pub(&pub, make_packet("msg #0")) == A0_OK);
    REQUIRE(a0_pub(&pub, make_packet("msg #1")) == A0_OK);

    REQUIRE(a0_publisher_close(&pub) == A0_OK);

    REQUIRE(a0_shmobj_close(&shmobj) == A0_OK);
  }

  a0_alephzero_t alephzero;
  REQUIRE(a0_alephzero_init(&alephzero) == A0_OK);

  {
    a0_subscriber_sync_t sub;
    REQUIRE(a0_subscriber_sync_init(&sub, alephzero, "sub_topic", A0_INIT_OLDEST, A0_ITER_NEXT) ==
            A0_OK);

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
                                    alephzero,
                                    "sub_topic",
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

  REQUIRE(a0_alephzero_close(&alephzero) == A0_OK);
}
