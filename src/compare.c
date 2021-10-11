#include <a0/compare.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/unused.h>
#include <a0/uuid.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

static const uint32_t GOLDEN_RATIO_U32 = 0x9E3779B9;

//////////////////////
// Compare uint32_t //
//////////////////////

// https://softwareengineering.stackexchange.com/questions/402542/where-do-magic-hashing-constants-like-0x9e3779b9-and-0x9e3779b1-come-from
a0_err_t a0_hash_u32_fn(void* user_data, const void* data, size_t* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = (*(uint32_t*)data) * GOLDEN_RATIO_U32;
  return A0_OK;
}

const a0_hash_t A0_HASH_U32 = {
    .user_data = NULL,
    .fn = a0_hash_u32_fn,
};

a0_err_t a0_compare_u32_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  int64_t diff = (int64_t)(*(uint32_t*)lhs) - (int64_t)(*(uint32_t*)rhs);
  *out = (diff < 0) - (diff > 0);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_U32 = {
    .user_data = NULL,
    .fn = a0_compare_u32_fn,
};

//////////////////////
// Compare pointers //
//////////////////////

// https://stackoverflow.com/questions/20953390/what-is-the-fastest-hash-function-for-pointers
a0_err_t a0_hash_ptr_fn(void* user_data, const void* data, size_t* out) {
  A0_MAYBE_UNUSED(user_data);
  const size_t shift = (size_t)log2(1 + sizeof(uintptr_t));
  *out = (*(uintptr_t*)data) >> shift;
  return A0_OK;
}

const a0_hash_t A0_HASH_PTR = {
    .user_data = NULL,
    .fn = a0_hash_ptr_fn,
};

a0_err_t a0_compare_ptr_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = (int)(*(uintptr_t*)lhs - *(uintptr_t*)rhs);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_PTR = {
    .user_data = NULL,
    .fn = a0_compare_ptr_fn,
};

//////////////////////
// Compare C-String //
//////////////////////

// https://softwareengineering.stackexchange.com/questions/402542/where-do-magic-hashing-constants-like-0x9e3779b9-and-0x9e3779b1-come-from
a0_err_t a0_hash_str_fn(void* user_data, const void* data, size_t* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = 0;
  for (char* c = *(char**)data; *c; ++c) {
    *out ^= *c + GOLDEN_RATIO_U32 + (*out << 6) + (*out >> 2);
  }
  return A0_OK;
}

const a0_hash_t A0_HASH_STR = {
    .user_data = NULL,
    .fn = a0_hash_str_fn,
};

a0_err_t a0_compare_str_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = strcmp(*(char**)lhs, *(char**)rhs);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_STR = {
    .user_data = NULL,
    .fn = a0_compare_str_fn,
};

//////////////////
// Compare UUID //
//////////////////

// clang-format off
static const uint8_t UNHEX_VALUES[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,
    0, 10, 11, 12, 13, 14, 15,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0, 10, 11, 12, 13, 14, 15,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};
// clang-format on

A0_STATIC_INLINE
a0_err_t a0_hash_uuid_fn(void* user_data, const void* data, size_t* out) {
  A0_MAYBE_UNUSED(user_data);
  // UUID is 16 bytes stretched to 37 bytes.
  // The hash is computed by recovering some of the random bytes from the uuid.
  //
  // Each random byte is replaced by two bytes that are human-readable.
  // 4 bytes are added as dashes.
  // 1 byte is the uuid version.
  // 1 byte is the uuid variant.
  //
  // In the unhexing process, we exclude bytes:
  //    8, 13, 18, and 23 ... for being dashes.
  //   14                 ... for being a version.
  //   19                 ... for being a variant.
  uint8_t* uuid = (uint8_t*)data;
  uint8_t* hash_bytes = (uint8_t*)out;
  hash_bytes[0] = (UNHEX_VALUES[uuid[0]] << 4) | UNHEX_VALUES[uuid[1]];
  hash_bytes[1] = (UNHEX_VALUES[uuid[2]] << 4) | UNHEX_VALUES[uuid[3]];
  hash_bytes[2] = (UNHEX_VALUES[uuid[4]] << 4) | UNHEX_VALUES[uuid[5]];
  hash_bytes[3] = (UNHEX_VALUES[uuid[6]] << 4) | UNHEX_VALUES[uuid[7]];
  hash_bytes[4] = (UNHEX_VALUES[uuid[9]] << 4) | UNHEX_VALUES[uuid[10]];
  hash_bytes[5] = (UNHEX_VALUES[uuid[11]] << 4) | UNHEX_VALUES[uuid[12]];
  hash_bytes[6] = (UNHEX_VALUES[uuid[13]] << 4) | UNHEX_VALUES[uuid[15]];
  hash_bytes[7] = (UNHEX_VALUES[uuid[16]] << 4) | UNHEX_VALUES[uuid[17]];

  return A0_OK;
}

const a0_hash_t A0_HASH_UUID = {
    .user_data = NULL,
    .fn = a0_hash_uuid_fn,
};

a0_err_t a0_compare_uuid_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = memcmp(lhs, rhs, A0_UUID_SIZE);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_UUID = {
    .user_data = NULL,
    .fn = a0_compare_uuid_fn,
};
