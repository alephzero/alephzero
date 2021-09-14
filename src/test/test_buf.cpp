#include <a0/buf.hpp>

#include <doctest.h>

#include <cstdint>

TEST_CASE("buf] cpp") {
  uint8_t data[4];
  a0::Buf buf(data, 4);

  REQUIRE(buf.data() == data);
  REQUIRE(buf.size() == 4);
}
