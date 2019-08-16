#include <a0/packet.h>

#include <a0/internal/test_util.hh>

#include <doctest.h>

#include <map>
#include <vector>

TEST_CASE("Test packet") {
  size_t num_headers = 3;
  a0_packet_header_t headers[num_headers];
  headers[0] = {
      .key = a0::test::buf("key0"),
      .val = a0::test::buf("val0"),
  };
  headers[1] = {
      .key = a0_packet_dep_key(),
      .val = a0::test::buf("00000000-0000-0000-0000-000000000000"),
  };
  headers[2] = {
      .key = a0_packet_dep_key(),
      .val = a0::test::buf("00000000-0000-0000-0000-000000000001"),
  };

  a0_buf_t payload_buf = a0::test::buf("Hello, World!");

  REQUIRE(payload_buf.size == 13);
  uint8_t write_buf[226];
  a0_alloc_t alloc;
  alloc.user_data = write_buf;
  alloc.fn = [](void* user_data, size_t size, a0_buf_t* buf) {
    REQUIRE(size == 226);
    buf->size = 226;
    buf->ptr = (uint8_t*)user_data;
  };

  a0_packet_t pkt;
  REQUIRE(a0_packet_build(num_headers, headers, payload_buf, alloc, &pkt) == A0_OK);

  size_t read_num_header;
  REQUIRE(a0_packet_num_headers(pkt, &read_num_header) == A0_OK);
  REQUIRE(read_num_header == 4);

  std::map<std::string, std::vector<std::string>> read_hdrs;
  for (size_t i = 0; i < read_num_header; i++) {
    a0_packet_header_t hdr;
    REQUIRE(a0_packet_header(pkt, i, &hdr) == A0_OK);
    read_hdrs[a0::test::str(hdr.key)].push_back(a0::test::str(hdr.val));
  }
  REQUIRE(read_hdrs["key0"][0] == "val0");
  REQUIRE(read_hdrs["a0_dep"].size() == 2);
  REQUIRE(read_hdrs.count("a0_id"));

  a0_buf_t id;
  REQUIRE(a0_packet_id(pkt, &id) == A0_OK);
  REQUIRE(id.size == 36);

  a0_buf_t read_payload;
  REQUIRE(a0_packet_payload(pkt, &read_payload) == A0_OK);
  CHECK(a0::test::str(read_payload).size() == a0::test::str(payload_buf).size());
  REQUIRE(a0::test::str(read_payload) == "Hello, World!");
}
