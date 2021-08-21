#include <a0/buf.hpp>

#include <doctest.h>

TEST_CASE("buf] cpp") {
  uint8_t data[4];
  a0::Buf buf(data, 4);

  REQUIRE(buf.ptr() == data);
  REQUIRE(buf.size() == 4);
}
