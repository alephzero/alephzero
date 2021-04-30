#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <algorithm>
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
  for (auto* block = &hdr_block; block; block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      auto& hdr = block->headers[i];
      kv[hdr.key] = hdr.val;
    }
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
    REQUIRE_OK(a0_packet_deserialize(fpkt, a0::test::alloc(), &pkt_after));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(map_from(pkt_after.headers_block) == standard_packet_hdrs());
  });
}

TEST_CASE("packet] deep_copy") {
  with_standard_packet([](a0_packet_t pkt) {
    a0_packet_t pkt_after;
    REQUIRE_OK(a0_packet_deep_copy(pkt, a0::test::alloc(), &pkt_after));

    REQUIRE(std::string(pkt.id) == std::string(pkt_after.id));
    REQUIRE(a0::test::str(pkt.payload) == a0::test::str(pkt_after.payload));

    REQUIRE(map_from(pkt_after.headers_block) == standard_packet_hdrs());
  });
}

TEST_CASE("packet] dealloc") {
  a0_packet_header_t grp_a[2] = {
      {"a", "b"},
      {"c", "d"},
  };
  a0_packet_headers_block_t blk_a = {grp_a, 2, nullptr};

  a0_packet_t pkt_before;
  REQUIRE_OK(a0_packet_init(&pkt_before));
  pkt_before.headers_block = blk_a;
  pkt_before.payload = a0::test::buf("Hello, World!");

  struct data_t {
    bool was_alloc_called;
    bool was_dealloc_called;
    a0_buf_t expected_buf;
  } data{false, false, {}};

  // clang-format off
  a0_alloc_t alloc = {
      .user_data = &data,
      .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
        out->size = size;
        out->ptr = (uint8_t*)malloc(size);

        ((data_t*)user_data)->was_alloc_called = true;
        ((data_t*)user_data)->expected_buf = *out;

        return A0_OK;
      },
      .dealloc = [](void* user_data, a0_buf_t buf) {
        auto* data = (data_t*)user_data;
        REQUIRE(data->was_alloc_called);
        data->was_dealloc_called = true;
        REQUIRE(buf.ptr == data->expected_buf.ptr);
        REQUIRE(buf.size == data->expected_buf.size);
        free(buf.ptr);
        return A0_OK;
      },
  };
  // clang-format on

  a0_packet_t pkt_after;
  REQUIRE_OK(a0_packet_deep_copy(pkt_before, alloc, &pkt_after));
  REQUIRE(data.was_alloc_called);
  REQUIRE(!data.was_dealloc_called);
  REQUIRE_OK(a0_packet_dealloc(pkt_after, alloc));
  REQUIRE(data.was_dealloc_called);
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
