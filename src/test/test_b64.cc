#include <a0/b64.h>

#include <a0/internal/test_util.hh>
#include <catch.hpp>
#include <string>

TEST_CASE("Test base64 encode/decode", "[base64]") {
  uint8_t msg[] = "Hello, World!";

  a0_buf_t raw;
  raw.ptr = msg;
  raw.size = sizeof(msg);

  a0_alloc_t allocator;
  uint8_t encode_space[20];
  allocator.user_data = encode_space;
  allocator.callback = [](void* user_data, size_t size, a0_buf_t* out) {
    out->size = size;
    out->ptr = (uint8_t*)user_data;
  };

  a0_buf_t encoded;
  REQUIRE(b64_encode(&raw, &allocator, &encoded) == A0_OK);
  REQUIRE(encoded.size == 20);
  REQUIRE(str(encoded) == "SGVsbG8sIFdvcmxkIQA=");

  uint8_t decode_space[14];
  allocator.user_data = decode_space;

  a0_buf_t decoded;
  REQUIRE(b64_decode(&encoded, &allocator, &decoded) == A0_OK);
  REQUIRE(decoded.size == 14);
  REQUIRE(str(decoded) == str(raw));
}
