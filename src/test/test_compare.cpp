#include <a0/compare.h>
#include <a0/buf.hpp>

#include <doctest.h>

#include <cstdint>

TEST_CASE("compare] u32") {
  uint32_t a = 1;
  uint32_t b = 2;

  int cmp;
  a0_compare_eval(A0_COMPARE_U32, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_compare_eval(A0_COMPARE_U32, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_compare_eval(A0_COMPARE_U32, &b, &a, &cmp);
  REQUIRE(cmp > 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_U32, &a, &a_hash);
  a0_hash_eval(A0_HASH_U32, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("compare] ptr") {
  uintptr_t a = 0xAAAAAAAAAAAA;
  uintptr_t b = 0xBBBBBBBBBBBB;

  int cmp;
  a0_compare_eval(A0_COMPARE_PTR, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_compare_eval(A0_COMPARE_PTR, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_compare_eval(A0_COMPARE_PTR, &b, &a, &cmp);
  REQUIRE(cmp > 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_PTR, &a, &a_hash);
  a0_hash_eval(A0_HASH_PTR, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("compare] buf") {
  a0_buf_t a = {(uint8_t*)"aaa", 3};
  a0_buf_t b = {(uint8_t*)"bbb", 3};
  a0_buf_t c = {(uint8_t*)"cccc", 4};

  int cmp;
  a0_compare_eval(A0_COMPARE_BUF, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_compare_eval(A0_COMPARE_BUF, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_compare_eval(A0_COMPARE_BUF, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_compare_eval(A0_COMPARE_BUF, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_BUF, &a, &a_hash);
  a0_hash_eval(A0_HASH_BUF, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("compare] str") {
  const char* a = "aaa";
  const char* b = "bbb";
  const char* c = "cccc";

  int cmp;
  a0_compare_eval(A0_COMPARE_STR, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_compare_eval(A0_COMPARE_STR, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_compare_eval(A0_COMPARE_STR, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_compare_eval(A0_COMPARE_STR, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_STR, &a, &a_hash);
  a0_hash_eval(A0_HASH_STR, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("compare] uuid") {
  const char* a = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  const char* b = "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb";
  const char* c = "cccccccc-cccc-cccc-cccc-cccccccccccc";

  int cmp;
  a0_compare_eval(A0_COMPARE_UUID, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_compare_eval(A0_COMPARE_UUID, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_compare_eval(A0_COMPARE_UUID, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_compare_eval(A0_COMPARE_UUID, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_UUID, &a, &a_hash);
  a0_hash_eval(A0_HASH_UUID, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}
