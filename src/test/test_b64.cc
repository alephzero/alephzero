#include <a0/b64.h>

#include <string>

#include <catch.hpp>

TEST_CASE("Test base64 encode/decode", "[base64]") {
  uint8_t src[] = "Hello, World!";

  uint8_t* encoded;
  size_t encoded_size;
  REQUIRE(b64_encode(src, sizeof(src), &encoded, &encoded_size) == A0_OK);
  REQUIRE("SGVsbG8sIFdvcmxkIQA=" == std::string((char*)encoded, encoded_size));

  uint8_t* decoded;
  size_t decoded_size;
  REQUIRE(b64_decode(encoded, encoded_size, &decoded, &decoded_size) == A0_OK);
  REQUIRE(std::string((char*)src, sizeof(src)) ==
          std::string((char*)decoded, decoded_size));
}
