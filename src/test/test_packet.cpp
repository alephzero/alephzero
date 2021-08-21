#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/uuid.h>

#include <doctest.h>

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

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

  REQUIRE(pkt.payload.ptr == nullptr);
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

std::map<std::string, std::string> standard_packet_hdrs() {
  return {
      {"a", "b"},
      {"c", "d"},
      {"e", "f"},
      {"g", "h"},
      {"i", "j"},
  };
}

std::map<std::string, std::string> map_from(a0_packet_headers_block_t hdr_block) {
  std::map<std::string, std::string> kv;

  a0_packet_header_iterator_t iter;
  a0_packet_header_iterator_init(&iter, &hdr_block);

  a0_packet_header_t hdr;
  while (a0_packet_header_iterator_next(&iter, &hdr) == A0_OK) {
    kv[hdr.key] = hdr.val;
  }
  return kv;
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
    a0_buf_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    REQUIRE(fpkt.size == 166);

    a0_packet_t pkt_after;
    a0_buf_t unused;
    REQUIRE_OK(a0_packet_deserialize(fpkt, a0::test::alloc(), &pkt_after, &unused));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(map_from(pkt_after.headers_block) == standard_packet_hdrs());
  });
}

TEST_CASE("packet] deep_copy") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_packet_t pkt_after;
    a0_buf_t unused;
    REQUIRE_OK(a0_packet_deep_copy(pkt, a0::test::alloc(), &pkt_after, &unused));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(map_from(pkt_after.headers_block) == standard_packet_hdrs());
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
    REQUIRE(std::string((char*)flat_payload.ptr, flat_payload.size) == "Hello, World!");
  });
}

TEST_CASE("flat_packet] header") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_flat_packet_t fpkt;
    REQUIRE_OK(a0_packet_serialize(pkt, a0::test::alloc(), &fpkt));

    a0_packet_header_t flat_hdr;
    std::map<std::string, std::string> found_hdrs;
    for (size_t i = 0; i < 5; i++) {
      REQUIRE_OK(a0_flat_packet_header(fpkt, i, &flat_hdr));
      found_hdrs[flat_hdr.key] = flat_hdr.val;
    }
    REQUIRE(a0_flat_packet_header(fpkt, 5, &flat_hdr) == EINVAL);

    REQUIRE(found_hdrs == standard_packet_hdrs());
  });
}

TEST_CASE("packet] cpp") {
  a0::Packet pkt1({{"hdr-key", "hdr-val"}}, "Hello, World!");
  REQUIRE(pkt1.payload() == "Hello, World!");
  REQUIRE(pkt1.headers().size() == 1);
  REQUIRE(pkt1.id().size() == 36);

  REQUIRE(pkt1.headers()[0].first == "hdr-key");
  REQUIRE(pkt1.headers()[0].second == "hdr-val");

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
