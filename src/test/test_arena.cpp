#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.hpp>

#include <doctest.h>

#include <cstdint>

TEST_CASE("arena] cpp") {
  uint8_t data[4];
  a0::Buf buf(data, 4);

  a0::Arena arena(buf, A0_ARENA_MODE_SHARED);

  REQUIRE(arena.mode() == A0_ARENA_MODE_SHARED);
  REQUIRE(arena.buf().ptr() == data);
  REQUIRE(((a0::Buf)arena).ptr() == data);
}
