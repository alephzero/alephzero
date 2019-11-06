#include <a0/alloc.h>

#include <doctest.h>

#include <map>
#include <vector>

#include "src/test_util.hpp"

TEST_CASE("Test malloc") {
  auto alloc = a0_malloc_allocator();

  a0_buf_t buf_0;
  alloc.fn(alloc.user_data, 10, &buf_0);
  REQUIRE(buf_0.size == 10);
  memcpy(buf_0.ptr, "foo", 3);

  a0_buf_t buf_1;
  alloc.fn(alloc.user_data, 10, &buf_1);
  REQUIRE(buf_1.size == 10);

  REQUIRE(buf_0.ptr != buf_1.ptr);
  REQUIRE(memcpy(buf_0.ptr, "foo", 3));

  free(buf_0.ptr);
  free(buf_1.ptr);
  a0_free_malloc_allocator(alloc);
}

TEST_CASE("Test realloc") {
  auto alloc = a0_realloc_allocator();
  a0_buf_t buf_0;
  alloc.fn(alloc.user_data, 10, &buf_0);
  REQUIRE(buf_0.size == 10);
  memcpy(buf_0.ptr, "foo\0", 4);

  a0_buf_t buf_1;
  alloc.fn(alloc.user_data, 10, &buf_1);
  REQUIRE(buf_1.size == 10);

  REQUIRE(buf_0.ptr == buf_1.ptr);

  a0_free_realloc_allocator(alloc);
}
