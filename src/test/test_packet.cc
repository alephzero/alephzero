#include <a0/packet.h>

#include <doctest.h>

#include <map>
#include <vector>

#include "src/test_util.hh"

TEST_CASE("Test packet") {
  size_t num_headers = 3;
  a0_packet_header_t headers[num_headers];
  headers[0] = {
      .key = "key0",
      .val = "val0",
  };
  headers[1] = {
      .key = a0_packet_dep_key(),
      .val = "00000000-0000-0000-0000-000000000000",
  };
  headers[2] = {
      .key = a0_packet_dep_key(),
      .val = "00000000-0000-0000-0000-000000000001",
  };

  a0_buf_t payload_buf = a0::test::buf("Hello, World!");

  REQUIRE(payload_buf.size == 13);

  a0_packet_t pkt;
  REQUIRE(a0_packet_build(num_headers, headers, payload_buf, a0::test::allocator(), &pkt) == A0_OK);

  size_t read_num_header;
  REQUIRE(a0_packet_num_headers(pkt, &read_num_header) == A0_OK);
  REQUIRE(read_num_header == 4);

  std::map<std::string, std::vector<std::string>> read_hdrs;
  for (size_t i = 0; i < read_num_header; i++) {
    a0_packet_header_t hdr;
    REQUIRE(a0_packet_header(pkt, i, &hdr) == A0_OK);
    read_hdrs[hdr.key].push_back(hdr.val);
  }
  REQUIRE(read_hdrs["key0"][0] == "val0");
  REQUIRE(read_hdrs["a0_dep"].size() == 2);
  REQUIRE(read_hdrs.count("a0_id"));

  const char* find_val;
  size_t find_val_idx;
  REQUIRE(a0_packet_find_header(pkt, "key0", 0, &find_val, &find_val_idx) == A0_OK);
  REQUIRE(std::string(find_val) == "val0");
  REQUIRE(find_val_idx >= 0);
  REQUIRE(find_val_idx < 4);

  REQUIRE(a0_packet_find_header(pkt, "notkey0", 0, &find_val, &find_val_idx) == ENOKEY);

  a0_packet_id_t id;
  REQUIRE(a0_packet_id(pkt, &id) == A0_OK);
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      REQUIRE(id[i] == '-');
    } else {
      bool is_alphanum = isalpha(id[i]) || isdigit(id[i]);
      REQUIRE(is_alphanum);
    }
  }

  a0_buf_t read_payload;
  REQUIRE(a0_packet_payload(pkt, &read_payload) == A0_OK);
  CHECK(a0::test::str(read_payload).size() == a0::test::str(payload_buf).size());
  REQUIRE(a0::test::str(read_payload) == "Hello, World!");
}

TEST_CASE("Test packet build with id") {
  size_t num_headers = 1;
  a0_packet_header_t headers[num_headers];
  headers[0] = {
      .key = a0_packet_id_key(),
      .val = "some_id",
  };
  a0_buf_t payload_buf = a0::test::buf("Hello, World!");
  REQUIRE(a0_packet_build(num_headers, headers, payload_buf, a0::test::allocator(), nullptr) ==
          EINVAL);
}
