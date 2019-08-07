#include <a0/packet.h>

#include <a0/internal/test_util.hh>

#include <doctest.h>

TEST_CASE("Test packet") {
  uint8_t header0_key[] = "key0";
  uint8_t header0_val[] = "val0";
  uint8_t header1_key[] = "key 1";
  uint8_t header1_val[] = "val 1";
  uint8_t payload[] = "Hello, World!";

  size_t num_headers = 2;
  a0_packet_header_t headers[2];

  headers[0].key.size = sizeof(header0_key);
  headers[0].key.ptr = header0_key;
  headers[0].val.size = sizeof(header0_val);
  headers[0].val.ptr = header0_val;

  headers[1].key.size = sizeof(header1_key);
  headers[1].key.ptr = header1_key;
  headers[1].val.size = sizeof(header1_val);
  headers[1].val.ptr = header1_val;

  a0_buf_t payload_buf = {
      .ptr = payload,
      .size = sizeof(payload),
  };

  REQUIRE(payload_buf.size == 14);
  uint8_t write_buf[84];
  a0_alloc_t alloc;
  alloc.user_data = write_buf;
  alloc.fn = [](void* user_data, size_t size, a0_buf_t* buf) {
    REQUIRE(size == 84);
    buf->size = 84;
    buf->ptr = (uint8_t*)user_data;
  };

  a0_packet_t pkt;
  REQUIRE(a0_packet_build(num_headers, headers, payload_buf, alloc, &pkt) == A0_OK);

  size_t read_num_header;
  REQUIRE(a0_packet_num_headers(pkt, &read_num_header) == A0_OK);
  REQUIRE(read_num_header == 2);

  a0_packet_header_t read_header0;
  REQUIRE(a0_packet_header(pkt, 0, &read_header0) == A0_OK);
  REQUIRE(str(read_header0.key) == str(headers[0].key));
  REQUIRE(str(read_header0.val) == str(headers[0].val));

  a0_packet_header_t read_header1;
  REQUIRE(a0_packet_header(pkt, 1, &read_header1) == A0_OK);
  REQUIRE(str(read_header1.key) == str(headers[1].key));
  REQUIRE(str(read_header1.val) == str(headers[1].val));

  a0_buf_t read_payload;
  REQUIRE(a0_packet_payload(pkt, &read_payload) == A0_OK);
  REQUIRE(str(read_payload) == str(payload_buf));
}
