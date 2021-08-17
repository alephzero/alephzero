#include <a0/compare.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/unused.h>
#include <a0/uuid.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

//////////////////////
// Compare pointers //
//////////////////////

// https://stackoverflow.com/questions/20953390/what-is-the-fastest-hash-function-for-pointers
errno_t a0_hash_ptr_fn(void* user_data, const void* data, size_t* out) {
  A0_MAYBE_UNUSED(user_data);
  const size_t shift = (size_t)log2(1 + sizeof(uintptr_t));
  *out = (*(uintptr_t*)data) >> shift;
  return A0_OK;
}

const a0_hash_t A0_HASH_PTR = {
    .user_data = NULL,
    .fn = a0_hash_ptr_fn,
};

errno_t a0_compare_ptr_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = (*(uintptr_t*)lhs - *(uintptr_t*)rhs);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_PTR = {
    .user_data = NULL,
    .fn = a0_compare_ptr_fn,
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
errno_t a0_hash_uuid_fn(void* user_data, const void* data, size_t* out) {
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

errno_t a0_compare_uuid_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = memcmp(lhs, rhs, A0_UUID_SIZE);
  return A0_OK;
}

const a0_compare_t A0_COMPARE_UUID = {
    .user_data = NULL,
    .fn = a0_compare_uuid_fn,
};
