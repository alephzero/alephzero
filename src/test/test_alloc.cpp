#include <a0/alloc.h>
#include <a0/common.h>

#include <doctest.h>

#include <cstring>

#include "src/test_util.hpp"

TEST_CASE("alloc] malloc") {
  a0_alloc_t alloc;
  REQUIRE_OK(a0_malloc_allocator_init(&alloc));

  a0_buf_t buf_0;
  a0_alloc(alloc, 10, &buf_0);
  REQUIRE(buf_0.size == 10);
  memcpy(buf_0.ptr, "foo", 3);

  a0_buf_t buf_1;
  a0_alloc(alloc, 10, &buf_1);
  REQUIRE(buf_1.size == 10);

  REQUIRE(buf_0.ptr != buf_1.ptr);
  REQUIRE(memcpy(buf_0.ptr, "foo", 3));

  REQUIRE_OK(a0_dealloc(alloc, buf_0));
  REQUIRE_OK(a0_dealloc(alloc, buf_1));
  REQUIRE_OK(a0_malloc_allocator_close(&alloc));
}

TEST_CASE("alloc] realloc") {
  a0_alloc_t alloc;
  REQUIRE_OK(a0_realloc_allocator_init(&alloc));

  a0_buf_t buf_0;
  a0_alloc(alloc, 10, &buf_0);
  REQUIRE(buf_0.size == 10);
  memcpy(buf_0.ptr, "foo\0", 4);

  a0_buf_t buf_1;
  a0_alloc(alloc, 10, &buf_1);
  REQUIRE(buf_1.size == 10);

  REQUIRE(buf_0.ptr == buf_1.ptr);

  REQUIRE_OK(a0_dealloc(alloc, buf_0));
  REQUIRE_OK(a0_dealloc(alloc, buf_1));
  REQUIRE_OK(a0_realloc_allocator_close(&alloc));
}
