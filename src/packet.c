#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/packet.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "macros.h"
#include "rand.h"

static const char kIdKey[] = "a0_id";
static const char kDepKey[] = "a0_dep";

const char* a0_packet_id_key() {
  return kIdKey;
}

const char* a0_packet_dep_key() {
  return kDepKey;
}

errno_t a0_packet_num_headers(const a0_packet_t pkt, size_t* out) {
  *out = *(size_t*)pkt.ptr;
  return A0_OK;
}

errno_t a0_packet_header(const a0_packet_t pkt, size_t hdr_idx, a0_packet_header_t* out) {
  size_t num_hdrs;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(pkt, &num_hdrs));
  if (A0_UNLIKELY(hdr_idx >= num_hdrs)) {
    return EINVAL;
  }

  size_t key_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 0) * sizeof(size_t)));
  size_t val_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 1) * sizeof(size_t)));

  out->key = (char*)(pkt.ptr + key_off);
  out->val = (char*)(pkt.ptr + val_off);

  return A0_OK;
}

errno_t a0_packet_payload(const a0_packet_t pkt, a0_buf_t* out) {
  size_t num_header = *(size_t*)pkt.ptr;
  size_t payload_off = *(size_t*)(pkt.ptr + sizeof(size_t) + (2 * num_header) * sizeof(size_t));

  *out = (a0_buf_t){
      .ptr = pkt.ptr + payload_off,
      .size = pkt.size - payload_off,
  };

  return A0_OK;
}

errno_t a0_packet_find_header(const a0_packet_t pkt,
                              const char* key,
                              size_t start_search_idx,
                              const char** val_out,
                              size_t* idx_out) {
  size_t num_hdrs = 0;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(pkt, &num_hdrs));
  for (size_t i = start_search_idx; i < num_hdrs; i++) {
    a0_packet_header_t hdr;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_header(pkt, i, &hdr));
    if (!strcmp(key, (char*)hdr.key)) {
      if (val_out) {
        *val_out = hdr.val;
      }
      if (idx_out) {
        *idx_out = i;
      }
      return A0_OK;
    }
  }
  return ENOKEY;
}

errno_t a0_packet_headers(const a0_packet_t pkt, size_t num_hdrs, a0_packet_headers_block_t* out) {
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(pkt, &out->size));
  if (out->size > num_hdrs) {
    out->size = num_hdrs;
  }
  for (size_t i = 0; i < out->size; i++) {
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_header(pkt, i, &out->headers[i]));
  }
  out->next_block = NULL;
  return A0_OK;
}

errno_t a0_packet_id(a0_packet_t pkt, a0_packet_id_t* out) {
  const char* val;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_find_header(pkt, a0_packet_id_key(), 0, &val, NULL));
  memcpy(*out, val, A0_PACKET_ID_SIZE);
  return A0_OK;
}

errno_t a0_packet_build_raw(const a0_packet_raw_t raw_pkt, a0_alloc_t alloc, a0_packet_t* out) {
  a0_packet_t unused_pkt;
  if (!out) {
    out = &unused_pkt;
  }

  size_t total_num_hdrs = 0;
  for (const a0_packet_headers_block_t* block = &raw_pkt.headers_block;
       block;
       block = block->next_block) {
    total_num_hdrs += block->size;
  }

  // Alloc out space.
  {
    size_t size = sizeof(size_t);                 // Num headers.
    size += 2 * total_num_hdrs * sizeof(size_t);  // Header offsets.
    size += sizeof(size_t);                       // Payload offset.
    for (const a0_packet_headers_block_t* block = &raw_pkt.headers_block;
         block;
         block = block->next_block) {
      for (size_t i = 0; i < block->size; i++) {
        a0_packet_header_t* hdr = &block->headers[i];
        size += strlen(hdr->key) + 1;  // Key content.
        size += strlen(hdr->val) + 1;  // Val content.
      }
    }
    size += raw_pkt.payload.size;

    alloc.fn(alloc.user_data, size, out);
  }

  // Number of headers.
  memcpy(out->ptr, &total_num_hdrs, sizeof(size_t));

  size_t idx_off = sizeof(size_t);
  size_t off = sizeof(size_t) + 2 * total_num_hdrs * sizeof(size_t) + sizeof(size_t);

  // For each header.
  for (const a0_packet_headers_block_t* block = &raw_pkt.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* hdr = &block->headers[i];

      // Header key offset.
      memcpy(out->ptr + idx_off, &off, sizeof(size_t));
      idx_off += sizeof(size_t);

      // Header key content.
      memcpy(out->ptr + off, hdr->key, strlen(hdr->key) + 1);
      off += strlen(hdr->key) + 1;

      // Header val offset.
      memcpy(out->ptr + idx_off, &off, sizeof(size_t));
      idx_off += sizeof(size_t);

      // Header val content.
      memcpy(out->ptr + off, hdr->val, strlen(hdr->val) + 1);
      off += strlen(hdr->val) + 1;
    }
  }

  memcpy(out->ptr + idx_off, &off, sizeof(size_t));

  // Payload.
  memcpy(out->ptr + off, raw_pkt.payload.ptr, raw_pkt.payload.size);

  return A0_OK;
}

errno_t a0_packet_build(const a0_packet_raw_t raw_pkt, a0_alloc_t alloc, a0_packet_t* out) {
  for (size_t i = 0; i < raw_pkt.headers_block.size; i++) {
    if (A0_UNLIKELY(!strcmp(kIdKey, (char*)raw_pkt.headers_block.headers[i].key))) {
      return EINVAL;
    }
  }

  a0_packet_id_t pkt_id;
  a0_uuidv4((uint8_t*)pkt_id);

  a0_packet_header_t id_hdr = {
      .key = kIdKey,
      .val = (const char*)pkt_id,
  };

  a0_packet_headers_block_t wrapped_headers_block = {
      .headers = &id_hdr,
      .size = 1,
      .next_block = (a0_packet_headers_block_t*)&raw_pkt.headers_block,
  };

  a0_packet_raw_t wrapped_pkt = {
      .headers_block = wrapped_headers_block,
      .payload = raw_pkt.payload,
  };
  return a0_packet_build_raw(wrapped_pkt, alloc, out);
}

errno_t a0_packet_copy_with_additional_headers(const a0_packet_headers_block_t extra_headers,
                                               const a0_packet_t in,
                                               a0_alloc_t alloc,
                                               a0_packet_t* out) {
  a0_buf_t payload;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_payload(in, &payload));

  size_t old_num_hdrs;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(in, &old_num_hdrs));

  // TODO: Don't limit here.
  if (old_num_hdrs >= 1024) {
    return E2BIG;
  }
  a0_packet_header_t old_headers[1024];

  a0_packet_headers_block_t old_headers_block;
  old_headers_block.headers = old_headers;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_headers(in, 1024, &old_headers_block));
  old_headers_block.next_block = (a0_packet_headers_block_t*)&extra_headers;

  a0_packet_raw_t raw_pkt = {
      .headers_block = old_headers_block,
      .payload = payload,
  };
  return a0_packet_build_raw(raw_pkt, alloc, out);
}
