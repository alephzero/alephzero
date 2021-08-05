#include "protocol_util.h"

#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/unused.h>
#include <a0/uuid.h>

#include <alloca.h>

#include "err_util.h"

errno_t a0_open_topic(const char* template,
                      const char* topic,
                      const a0_file_options_t* topic_opts,
                      a0_file_t* file) {
  if (topic[0] == '\0' || topic[0] == '/') {
    return EINVAL;
  }

  const size_t topic_len = strlen(topic);

  static const size_t MAX_MATCH_CNT = 4;
  const char* segment_starts[MAX_MATCH_CNT];
  size_t segment_size[MAX_MATCH_CNT];
  uint8_t num_matches = 0;

  size_t len = 0;
  const char* prev = template;
  const char* next;
  while ((next = strstr(prev, "{topic}")) && num_matches < MAX_MATCH_CNT) {
    segment_starts[num_matches] = prev;
    segment_size[num_matches] = next - prev;
    num_matches++;

    len += next - prev + topic_len;
    prev = next + strlen("{topic}");
  }
  size_t tail_len = strlen(prev);
  len += tail_len;

  char* path = (char*)alloca(len + 1);
  char* write_ptr = path;
  for (size_t i = 0; i < num_matches; ++i) {
    strncpy(write_ptr, segment_starts[i], segment_size[i]);
    write_ptr += segment_size[i];
    strcpy(write_ptr, topic);
    write_ptr += topic_len;
  }
  strcpy(write_ptr, prev);
  write_ptr += tail_len;
  *write_ptr = '\0';
  return a0_file_open(path, topic_opts, file);
}

errno_t a0_find_header(a0_packet_t pkt, const char* key, const char** val) {
  a0_packet_header_iterator_t iter;
  A0_RETURN_ERR_ON_ERR(a0_packet_header_iterator_init(
      &iter, &pkt.headers_block));

  a0_packet_header_t hdr;
  while (a0_packet_header_iterator_next(&iter, &hdr) == A0_OK) {
    if (!strcmp(hdr.key, key)) {
      *val = hdr.val;
      return A0_OK;
    }
  }
  return EINVAL;
}

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
errno_t a0_uuid_hash_fn(void* user_data, const void* data, size_t* out) {
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

const a0_hash_t A0_UUID_HASH = {
    .user_data = NULL,
    .fn = a0_uuid_hash_fn,
};

errno_t a0_uuid_compare_fn(void* user_data, const void* lhs, const void* rhs, int* out) {
  A0_MAYBE_UNUSED(user_data);
  *out = memcmp(lhs, rhs, A0_UUID_SIZE);
  return A0_OK;
}

const a0_compare_t A0_UUID_COMPARE = {
    .user_data = NULL,
    .fn = a0_uuid_compare_fn,
};
