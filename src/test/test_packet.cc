#include <a0/packet.h>

#include <a0/internal/test_util.hh>

#include <doctest.h>

#include <map>

TEST_CASE("Test packet") {
  size_t num_headers = 2;
  a0_packet_header_t headers[num_headers];
  headers[0].key = buf("key0");
  headers[0].val = buf("val0");
  headers[1].key = buf("key 1");
  headers[1].val = buf("val 1");

  a0_buf_t payload_buf = buf("Hello, World!");

  REQUIRE(payload_buf.size == 13);
  uint8_t write_buf[116];
  a0_alloc_t alloc;
  alloc.user_data = write_buf;
  alloc.fn = [](void* user_data, size_t size, a0_buf_t* buf) {
    REQUIRE(size == 116);
    buf->size = 116;
    buf->ptr = (uint8_t*)user_data;
  };

  a0_packet_t pkt;
  REQUIRE(a0_packet_build(num_headers, headers, payload_buf, alloc, &pkt) == A0_OK);

  size_t read_num_header;
  REQUIRE(a0_packet_num_headers(pkt, &read_num_header) == A0_OK);
  REQUIRE(read_num_header == 3);

  std::map<std::string, std::string> read_hdrs;
  for (size_t i = 0; i < read_num_header; i++) {
    a0_packet_header_t hdr;
    REQUIRE(a0_packet_header(pkt, i, &hdr) == A0_OK);
    read_hdrs[str(hdr.key)] = str(hdr.val);
  }
  REQUIRE(read_hdrs["key0"] == "val0");
  REQUIRE(read_hdrs["key 1"] == "val 1");
  REQUIRE(read_hdrs.count("a0_id"));

  a0_packet_id_t id = 0;
  REQUIRE(a0_packet_id(pkt, &id) == A0_OK);

  a0_buf_t read_payload;
  REQUIRE(a0_packet_payload(pkt, &read_payload) == A0_OK);
  CHECK(str(read_payload).size() == str(payload_buf).size());
  REQUIRE(str(read_payload) == "Hello, World!");
}
