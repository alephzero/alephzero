#include <a0/b64.h>

#include <a0/internal/test_util.hh>
#include <doctest.h>
#include <string>

TEST_CASE("Test base64 encode/decode") {
  uint8_t msg[] = "Hello, World!";

  a0_buf_t original;
  original.ptr = msg;
  original.size = sizeof(msg);

  a0_alloc_t alloc;
  uint8_t encode_space[20];
  alloc.user_data = encode_space;
  alloc.fn = [](void* user_data, size_t size, a0_buf_t* out) {
    out->size = size;
    out->ptr = (uint8_t*)user_data;
  };

  a0_buf_t encoded;
  REQUIRE(b64_encode(original, alloc, &encoded) == A0_OK);
  REQUIRE(encoded.size == 20);
  REQUIRE(str(encoded) == "SGVsbG8sIFdvcmxkIQA=");

  uint8_t decode_space[14];
  alloc.user_data = decode_space;

  a0_buf_t decoded;
  REQUIRE(b64_decode(encoded, alloc, &decoded) == A0_OK);
  REQUIRE(decoded.size == 14);
  REQUIRE(str(decoded) == str(original));
}

TEST_CASE("Test base64 encode/decode empty") {
  uint8_t msg[] = "";

  a0_buf_t original;
  original.ptr = msg;
  original.size = 0;

  a0_alloc_t alloc;
  uint8_t encode_space[0];
  alloc.user_data = encode_space;
  alloc.fn = [](void* user_data, size_t size, a0_buf_t* out) {
    out->size = size;
    out->ptr = (uint8_t*)user_data;
  };

  a0_buf_t encoded;
  REQUIRE(b64_encode(original, alloc, &encoded) == A0_OK);
  REQUIRE(encoded.size == 0);
  REQUIRE(str(encoded) == "");

  uint8_t decode_space[0];
  alloc.user_data = decode_space;

  a0_buf_t decoded;
  REQUIRE(b64_decode(encoded, alloc, &decoded) == A0_OK);
  REQUIRE(decoded.size == 0);
  REQUIRE(str(decoded) == str(original));
}
