#include <a0/pubsub.h>

#include <a0/internal/strutil.hh>
#include <a0/internal/test_util.hh>
#include <doctest.h>

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

    a0_alloc_t alloc;
    alloc.fn = [](void*, size_t size, a0_buf_t* buf) {
      buf->size = size;
      buf->ptr = (uint8_t*)malloc(size);
    };
    a0_packet_t pkt;
    REQUIRE(a0_packet_build(builder, alloc, &pkt) == A0_OK);

    return pkt;
  }

  void free_packet(a0_packet_t pkt) {
    free(pkt.ptr);
  }
};

TEST_CASE_FIXTURE(PubsubFixture, "Test sync pubsub") {
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
    REQUIRE(a0_subscriber_sync_open(&sub, topic, A0_READ_START_EARLIEST, A0_READ_NEXT_SEQUENTIAL) == A0_OK);

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
}
