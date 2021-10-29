#include <a0/buf.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/uuid.h>

#include <doctest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <unordered_map>

#include "src/test_util.hpp"

TEST_CASE("packet] init") {
  a0_packet_t pkt;
  REQUIRE_OK(a0_packet_init(&pkt));

  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      REQUIRE(pkt.id[i] == '-');
    } else {
      bool is_alphanum = isalpha(pkt.id[i]) || isdigit(pkt.id[i]);
      REQUIRE(is_alphanum);
    }
  }

  REQUIRE(pkt.headers_block.headers == nullptr);
  REQUIRE(pkt.headers_block.size == 0);
  REQUIRE(pkt.headers_block.next_block == nullptr);

  REQUIRE(pkt.payload.data == nullptr);
  REQUIRE(pkt.payload.size == 0);
}

void with_standard_packet(std::function<void(a0_packet_t pkt)> fn) {
  a0_packet_header_t grp_a[2] = {
      {"a", "b"},
      {"c", "d"},
  };
  a0_packet_headers_block_t blk_a = {grp_a, 2, nullptr};

  a0_packet_header_t grp_b[3] = {
      {"e", "f"},
      {"g", "h"},
      {"i", "j"},
  };
  a0_packet_headers_block_t blk_b = {grp_b, 3, &blk_a};

  a0_packet_t pkt;
  REQUIRE_OK(a0_packet_init(&pkt));
  pkt.headers_block = blk_b;
  pkt.payload = a0::test::buf("Hello, World!");

  fn(pkt);
}

std::unordered_multimap<std::string, std::string> standard_packet_hdrs() {
  return {
      {"a", "b"},
      {"c", "d"},
      {"e", "f"},
      {"g", "h"},
      {"i", "j"},
  };
}

TEST_CASE("packet] stats") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_packet_stats_t stats;
    REQUIRE_OK(a0_packet_stats(pkt, &stats));

    REQUIRE(stats.num_hdrs == 5);
    // Each header has a key & value (x2) with 2 chars each (including '\0').
    size_t want_content_size = (stats.num_hdrs * 2 * 2);
    // 13 for payload (not including '\0').
    want_content_size += 13;
    REQUIRE(stats.content_size == want_content_size);
    // Serialized buffer has the content
    size_t want_serial_size = want_content_size;
    // and an ID
    want_serial_size += A0_UUID_SIZE;
    // and the number of headers
    want_serial_size += sizeof(size_t);
    // and an offset for each header key & value
    want_serial_size += stats.num_hdrs * 2 * sizeof(size_t);
    // and an offset for the payload
    want_serial_size += sizeof(size_t);
    REQUIRE(stats.serial_size == want_serial_size);
  });
}

TEST_CASE("packet] serialize deserialize") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    REQUIRE(fpkt.buf.size == 166);

    a0_packet_t pkt_after;
    a0_buf_t unused;
    REQUIRE_OK(a0_packet_deserialize(fpkt, a0::test::alloc(), &pkt_after, &unused));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(a0::test::hdr(pkt_after) == standard_packet_hdrs());
  });
}

TEST_CASE("packet] deep_copy") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_packet_t pkt_after;
    a0_buf_t unused;
    REQUIRE_OK(a0_packet_deep_copy(pkt, a0::test::alloc(), &pkt_after, &unused));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(a0::test::hdr(pkt_after) == standard_packet_hdrs());
  });
}

TEST_CASE("flat_packet] stats") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    a0_packet_stats_t stats;
    REQUIRE_OK(a0_flat_packet_stats(fpkt, &stats));

    REQUIRE(stats.num_hdrs == 5);
    // Each header has a key & value (x2) with 2 chars each (including '\0').
    size_t want_content_size = (stats.num_hdrs * 2 * 2);
    // 13 for payload (not including '\0').
    want_content_size += 13;
    REQUIRE(stats.content_size == want_content_size);
    // Serialized buffer has the content
    size_t want_serial_size = want_content_size;
    // and an ID
    want_serial_size += A0_UUID_SIZE;
    // and the number of headers
    want_serial_size += sizeof(size_t);
    // and an offset for each header key & value
    want_serial_size += stats.num_hdrs * 2 * sizeof(size_t);
    // and an offset for the payload
    want_serial_size += sizeof(size_t);
    REQUIRE(stats.serial_size == want_serial_size);
  });
}

TEST_CASE("flat_packet] id") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    a0_uuid_t* fpkt_id;
    REQUIRE_OK(a0_flat_packet_id(fpkt, &fpkt_id));

    REQUIRE(std::string(pkt.id).size() == 36);
    REQUIRE(std::string(pkt.id) == std::string(*fpkt_id));
  });
}

TEST_CASE("flat_packet] payload") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    a0_buf_t flat_payload;
    REQUIRE_OK(a0_flat_packet_payload(fpkt, &flat_payload));

    REQUIRE(flat_payload.size == 13);
    REQUIRE(a0::test::str(flat_payload) == "Hello, World!");
  });
}

TEST_CASE("flat_packet] header") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    REQUIRE(a0::test::hdr(fpkt) == standard_packet_hdrs());
  });
}

TEST_CASE("packet] cpp") {
  a0::Packet pkt0;
  REQUIRE(pkt0.payload() == "");
  REQUIRE(pkt0.id().size() == 36);
  REQUIRE(pkt0.headers().empty());

  a0::Packet pkt1({{"hdr-key", "hdr-val"}}, "Hello, World!");
  REQUIRE(pkt1.payload() == "Hello, World!");
  REQUIRE(pkt1.id().size() == 36);
  REQUIRE(pkt1.headers() == std::unordered_multimap<std::string, std::string>{{"hdr-key", "hdr-val"}});

  a0::Packet pkt2 = pkt1;
  REQUIRE(pkt2.id() == pkt1.id());
  REQUIRE(pkt1.id() == pkt2.id());
  REQUIRE(pkt1.headers() == pkt2.headers());
  REQUIRE(pkt1.payload() == pkt2.payload());
  REQUIRE(pkt1.payload().data() == pkt2.payload().data());

  a0::Packet pkt3("Hello, World!");
  REQUIRE(pkt3.payload() == "Hello, World!");
  REQUIRE(pkt3.headers().empty());
  REQUIRE(pkt3.id().size() == 36);

  std::string owner = "Hello, World!";

  a0::Packet pkt4(owner);
  REQUIRE(pkt4.payload() == owner);
  REQUIRE(pkt4.payload().data() != owner.data());

  a0::Packet pkt5(owner, a0::ref);
  REQUIRE(pkt5.payload() == owner);
  REQUIRE(pkt5.payload().data() == owner.data());
}

TEST_CASE("flat_packet] cpp") {
  with_standard_packet([](a0_packet_t pkt) {
    a0::FlatPacket fpkt;
    fpkt.c = std::make_shared<a0_flat_packet_t>();
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), fpkt.c.get()));

    REQUIRE(fpkt.id().size() == 37);
    REQUIRE(fpkt.num_headers() == 5);

    std::unordered_multimap<a0::string_view, a0::string_view> hdrs;
    for (size_t i = 0; i < fpkt.num_headers(); i++) {
      hdrs.insert(fpkt.header(i));
    }
    REQUIRE(hdrs == std::unordered_multimap<a0::string_view, a0::string_view>{
        {"a", "b"},
        {"c", "d"},
        {"e", "f"},
        {"g", "h"},
        {"i", "j"},
    });
  });
}
