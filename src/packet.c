#include <a0/packet.h>

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

errno_t a0_packet_num_headers(a0_packet_t pkt, size_t* out) {
  *out = *(size_t*)pkt.ptr;
  return A0_OK;
}

errno_t a0_packet_header(a0_packet_t pkt, size_t hdr_idx, a0_packet_header_t* out) {
  // TODO: Verify enough headers.
  size_t key_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 0) * sizeof(size_t)));
  size_t val_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 1) * sizeof(size_t)));

  out->key = (char*)(pkt.ptr + key_off);
  out->val = (char*)(pkt.ptr + val_off);

  return A0_OK;
}

errno_t a0_packet_payload(a0_packet_t pkt, a0_buf_t* out) {
  size_t num_header = *(size_t*)pkt.ptr;
  size_t payload_off = *(size_t*)(pkt.ptr + sizeof(size_t) + (2 * num_header) * sizeof(size_t));

  *out = (a0_buf_t){
      .ptr = pkt.ptr + payload_off,
      .size = pkt.size - payload_off,
  };

  return A0_OK;
}

errno_t a0_packet_find_header(a0_packet_t pkt,
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

errno_t a0_packet_id(a0_packet_t pkt, a0_packet_id_t* out) {
  const char* val;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_find_header(pkt, a0_packet_id_key(), 0, &val, NULL));
  memcpy(*out, val, A0_PACKET_ID_SIZE);
  return A0_OK;
}

errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t alloc,
                        a0_packet_t* out) {
  a0_packet_t unused_pkt;
  if (!out) {
    out = &unused_pkt;
  }

  for (size_t i = 0; i < num_headers; i++) {
    if (A0_UNLIKELY(!strcmp(kIdKey, (char*)headers[i].key))) {
      return EINVAL;
    }
  }

  // Alloc out space.
  {
    size_t size = sizeof(size_t);  // Num headers.
    size += 2 * sizeof(size_t);  // Id offsets, if not already in headers.
    size += 2 * num_headers * sizeof(size_t);  // Header offsets.
    size += sizeof(size_t);                    // Payload offset.
    size += sizeof(kIdKey) + A0_PACKET_ID_SIZE;  // Id content, if not already in headers.
    for (size_t i = 0; i < num_headers; i++) {
      size += strlen(headers[i].key) + 1;  // Key content.
      size += strlen(headers[i].val) + 1;  // Val content.
    }
    size += payload.size;

    alloc.fn(alloc.user_data, size, out);
  }

  size_t total_headers = num_headers + 1;  // +1 for id

  size_t off = 0;

  // Number of headers.
  memcpy(out->ptr + off, &total_headers, sizeof(size_t));
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * total_headers * sizeof(size_t) + sizeof(size_t);

  // Id key offset.
  memcpy(out->ptr + idx_off, &off, sizeof(size_t));
  idx_off += sizeof(size_t);

  // Id key content.
  memcpy(out->ptr + off, kIdKey, sizeof(kIdKey));
  off += sizeof(kIdKey);

  // Id val offset.
  memcpy(out->ptr + idx_off, &off, sizeof(size_t));
  idx_off += sizeof(size_t);

  // Id val content.
  uuidv4(out->ptr + off);
  off += kUuidSize;

  // For each header.
  for (size_t i = 0; i < num_headers; i++) {
    // Header key offset.
    memcpy(out->ptr + idx_off, &off, sizeof(size_t));
    idx_off += sizeof(size_t);

    // Header key content.
    memcpy(out->ptr + off, headers[i].key, strlen(headers[i].key) + 1);
    off += strlen(headers[i].key) + 1;

    // Header val offset.
    memcpy(out->ptr + idx_off, &off, sizeof(size_t));
    idx_off += sizeof(size_t);

    // Header val content.
    memcpy(out->ptr + off, headers[i].val, strlen(headers[i].val) + 1);
    off += strlen(headers[i].val) + 1;
  }

  memcpy(out->ptr + idx_off, &off, sizeof(size_t));

  // Payload.
  if (payload.size) {
    memcpy(out->ptr + off, payload.ptr, payload.size);
  }

  return A0_OK;
}

errno_t a0_packet_copy_with_additional_headers(size_t num_headers,
                                               a0_packet_header_t* headers,
                                               a0_packet_t in,
                                               a0_alloc_t alloc,
                                               a0_packet_t* out) {
  a0_packet_t pkt;
  if (!out) {
    out = &pkt;
  }

  size_t expanded_size = in.size;
  for (size_t i = 0; i < num_headers; i++) {
    expanded_size += sizeof(size_t) + strlen(headers[i].key) + 1;
    expanded_size += sizeof(size_t) + strlen(headers[i].val) + 1;
  }
  alloc.fn(alloc.user_data, expanded_size, out);

  // Offsets into the pkt (r=read ptr) and frame data (w=write ptr).
  size_t roff = 0;
  size_t woff = 0;

  // Number of headers.
  size_t orig_num_headers = *(size_t*)(in.ptr + roff);
  size_t total_num_headers = orig_num_headers + num_headers;

  // Write in the new header count.
  memcpy(out->ptr + woff, &total_num_headers, sizeof(size_t));

  roff += sizeof(size_t);
  woff += sizeof(size_t);

  // Offset for the index table.
  size_t idx_roff = roff;
  size_t idx_woff = woff;

  // Update offsets past the end of the index table.
  roff += 2 * orig_num_headers * sizeof(size_t) + sizeof(size_t);
  woff += 2 * total_num_headers * sizeof(size_t) + sizeof(size_t);

  // Add new headers.
  for (size_t i = 0; i < num_headers; i++) {
    memcpy(out->ptr + idx_woff, &woff, sizeof(size_t));
    idx_woff += sizeof(size_t);

    memcpy(out->ptr + woff, headers[i].key, strlen(headers[i].key) + 1);
    woff += strlen(headers[i].key) + 1;

    memcpy(out->ptr + idx_woff, &woff, sizeof(size_t));
    idx_woff += sizeof(size_t);

    memcpy(out->ptr + woff, headers[i].val, strlen(headers[i].val) + 1);
    woff += strlen(headers[i].val) + 1;
  }

  // Add offsets for existing headers.
  size_t in_hdr0_off = 0;
  memcpy(&in_hdr0_off, in.ptr + sizeof(size_t), sizeof(size_t));
  for (size_t i = 0; i < 2 * orig_num_headers; i++) {
    size_t in_hdri_off = 0;
    memcpy(&in_hdri_off, in.ptr + idx_roff, sizeof(size_t));

    size_t updated_off = woff + in_hdri_off - in_hdr0_off;

    memcpy(out->ptr + idx_woff, &updated_off, sizeof(size_t));
    idx_woff += sizeof(size_t);
    idx_roff += sizeof(size_t);
  }

  // Add offset for payload.
  size_t in_payload_off = 0;
  memcpy(&in_payload_off, in.ptr + idx_roff, sizeof(size_t));

  size_t updated_payload_off = woff + in_payload_off - in_hdr0_off;
  memcpy(out->ptr + idx_woff, &updated_payload_off, sizeof(size_t));

  // Copy existing headers + payload.
  memcpy(out->ptr + woff, in.ptr + roff, in.size - roff);

  return A0_OK;
}
