#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

TEST_CASE("packet] stats") {
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
}

TEST_CASE("packet] for_each_header") {
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

  std::map<std::string, std::string> kv;
  // clang-format off
  REQUIRE_OK(a0_packet_for_each_header(blk_b, {
      .user_data = &kv,
      .fn = [](void* data, a0_packet_header_t hdr) {
        auto* map = (std::map<std::string, std::string>*)data;
        (*map)[hdr.key] = hdr.val;
      }
  }));
  // clang-format on

  REQUIRE(kv == std::map<std::string, std::string>{
                    {"a", "b"},
                    {"c", "d"},
                    {"e", "f"},
                    {"g", "h"},
                    {"i", "j"},
                });
}

TEST_CASE("packet] serialize deserialize") {
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

  a0_packet_t pkt_before;
  REQUIRE_OK(a0_packet_init(&pkt_before));
  pkt_before.headers_block = blk_b;
  pkt_before.payload = a0::test::buf("Hello, World!");

  a0_buf_t serial;
  REQUIRE_OK(a0_packet_serialize(pkt_before, a0::test::allocator(), &serial));

  REQUIRE(serial.size == 166);

  a0_packet_t pkt_after;
  REQUIRE_OK(a0_packet_deserialize(serial, a0::test::allocator(), &pkt_after));

  REQUIRE(std::string(pkt_before.id) == std::string(pkt_after.id));
  REQUIRE(a0::test::str(pkt_before.payload) == a0::test::str(pkt_after.payload));

  REQUIRE(pkt_after.headers_block.size == 5);

  std::map<std::string, std::string> kv;
  for (size_t i = 0; i < pkt_after.headers_block.size; i++) {
    auto& hdr = pkt_after.headers_block.headers[i];
    kv[hdr.key] = hdr.val;
  }

  REQUIRE(kv == std::map<std::string, std::string>{
                    {"a", "b"},
                    {"c", "d"},
                    {"e", "f"},
                    {"g", "h"},
                    {"i", "j"},
                });
}

TEST_CASE("packet] deep_copy") {
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

  a0_packet_t pkt_before;
  REQUIRE_OK(a0_packet_init(&pkt_before));
  pkt_before.headers_block = blk_b;
  pkt_before.payload = a0::test::buf("Hello, World!");

  a0_packet_t pkt_after;
  REQUIRE_OK(a0_packet_deep_copy(pkt_before, a0::test::allocator(), &pkt_after));

  REQUIRE(std::string(pkt_before.id) == std::string(pkt_after.id));
  REQUIRE(a0::test::str(pkt_before.payload) == a0::test::str(pkt_after.payload));

  REQUIRE(pkt_after.headers_block.size == 5);

  std::map<std::string, std::string> kv;
  for (size_t i = 0; i < pkt_after.headers_block.size; i++) {
    auto& hdr = pkt_after.headers_block.headers[i];
    kv[hdr.key] = hdr.val;
  }

  REQUIRE(kv == std::map<std::string, std::string>{
                    {"a", "b"},
                    {"c", "d"},
                    {"e", "f"},
                    {"g", "h"},
                    {"i", "j"},
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
