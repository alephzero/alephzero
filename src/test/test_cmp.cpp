#include <a0/buf.h>
#include <a0/cmp.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>

TEST_CASE("cmp] u32") {
  uint32_t a = 1;
  uint32_t b = 2;

  int cmp;
  a0_cmp_eval(A0_CMP_U32, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_cmp_eval(A0_CMP_U32, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_cmp_eval(A0_CMP_U32, &b, &a, &cmp);
  REQUIRE(cmp > 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_U32, &a, &a_hash);
  a0_hash_eval(A0_HASH_U32, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("cmp] ptr") {
  uintptr_t a = 0xAAAAAAAAAAAA;
  uintptr_t b = 0xBBBBBBBBBBBB;

  int cmp;
  a0_cmp_eval(A0_CMP_PTR, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_cmp_eval(A0_CMP_PTR, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_cmp_eval(A0_CMP_PTR, &b, &a, &cmp);
  REQUIRE(cmp > 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_PTR, &a, &a_hash);
  a0_hash_eval(A0_HASH_PTR, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("cmp] buf") {
  a0_buf_t a = {(uint8_t*)"aaa", 3};
  a0_buf_t b = {(uint8_t*)"bbb", 3};
  a0_buf_t c = {(uint8_t*)"cccc", 4};

  int cmp;
  a0_cmp_eval(A0_CMP_BUF, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_cmp_eval(A0_CMP_BUF, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_cmp_eval(A0_CMP_BUF, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_cmp_eval(A0_CMP_BUF, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_BUF, &a, &a_hash);
  a0_hash_eval(A0_HASH_BUF, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("cmp] str") {
  const char* a = "aaa";
  const char* b = "bbb";
  const char* c = "cccc";

  int cmp;
  a0_cmp_eval(A0_CMP_STR, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_cmp_eval(A0_CMP_STR, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_cmp_eval(A0_CMP_STR, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_cmp_eval(A0_CMP_STR, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_STR, &a, &a_hash);
  a0_hash_eval(A0_HASH_STR, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}

TEST_CASE("cmp] uuid") {
  a0_uuid_t a = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  a0_uuid_t b = "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb";
  a0_uuid_t c = "cccccccc-cccc-cccc-cccc-cccccccccccc";

  int cmp;
  a0_cmp_eval(A0_CMP_UUID, &a, &a, &cmp);
  REQUIRE(cmp == 0);
  a0_cmp_eval(A0_CMP_UUID, &a, &b, &cmp);
  REQUIRE(cmp < 0);
  a0_cmp_eval(A0_CMP_UUID, &b, &a, &cmp);
  REQUIRE(cmp > 0);
  a0_cmp_eval(A0_CMP_UUID, &a, &c, &cmp);
  REQUIRE(cmp < 0);

  size_t a_hash;
  size_t b_hash;

  a0_hash_eval(A0_HASH_UUID, &a, &a_hash);
  a0_hash_eval(A0_HASH_UUID, &b, &b_hash);

  REQUIRE(a_hash != b_hash);
}
