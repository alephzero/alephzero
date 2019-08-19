#include <a0/packet.h>

#include <a0/internal/macros.h>
#include <a0/internal/rand.h>

#include <stdio.h>
#include <string.h>

static const char kIdKey[] = "a0_id";
static const char kDepKey[] = "a0_dep";
static const size_t kUuidSize = 37;

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

errno_t a0_packet_find_header(a0_packet_t pkt, const char* key, const char** val_out) {
  size_t num_hdrs = 0;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(pkt, &num_hdrs));
  for (size_t i = 0; i < num_hdrs; i++) {
    a0_packet_header_t hdr;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_header(pkt, i, &hdr));
    if (!strcmp(key, (char*)hdr.key)) {
      *val_out = hdr.val;
      return A0_OK;
    }
  }
  return ENOKEY;
}

static const char kHexDigits[] =
    "000102030405060708090A0B0C0D0E0F"
    "101112131415161718191A1B1C1D1E1F"
    "202122232425262728292A2B2C2D2E2F"
    "303132333435363738393A3B3C3D3E3F"
    "404142434445464748494A4B4C4D4E4F"
    "505152535455565758595A5B5C5D5E5F"
    "606162636465666768696A6B6C6D6E6F"
    "707172737475767778797A7B7C7D7E7F"
    "808182838485868788898A8B8C8D8E8F"
    "909192939495969798999A9B9C9D9E9F"
    "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"
    "B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
    "C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF"
    "D0D1D2D3D4D5D6D7D8D9DADBDCDDDEDF"
    "E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF"
    "F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";

A0_STATIC_INLINE
void uuidv4(uint8_t out[37]) {
  uint32_t data[4] = {
      (uint32_t)mrand48(),
      ((uint32_t)mrand48() & 0xFF0FFFFF) | 0x00400000,
      ((uint32_t)mrand48() & 0xFFFFFF3F) | 0x00000080,
      (uint32_t)mrand48(),
  };

  uint8_t* bytes = (uint8_t*)data;

// GCC 5.4.0 complains about this.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
  *(uint16_t*)(&out[0]) = *(uint16_t*)(&kHexDigits[bytes[0] * 2]);
  *(uint16_t*)(&out[2]) = *(uint16_t*)(&kHexDigits[bytes[1] * 2]);
  *(uint16_t*)(&out[4]) = *(uint16_t*)(&kHexDigits[bytes[2] * 2]);
  *(uint16_t*)(&out[6]) = *(uint16_t*)(&kHexDigits[bytes[3] * 2]);
  out[8] = '-';
  *(uint16_t*)(&out[9]) = *(uint16_t*)(&kHexDigits[bytes[4] * 2]);
  *(uint16_t*)(&out[11]) = *(uint16_t*)(&kHexDigits[bytes[5] * 2]);
  out[13] = '-';
  *(uint16_t*)(&out[14]) = *(uint16_t*)(&kHexDigits[bytes[6] * 2]);
  *(uint16_t*)(&out[16]) = *(uint16_t*)(&kHexDigits[bytes[7] * 2]);
  out[18] = '-';
  *(uint16_t*)(&out[19]) = *(uint16_t*)(&kHexDigits[bytes[8] * 2]);
  *(uint16_t*)(&out[21]) = *(uint16_t*)(&kHexDigits[bytes[9] * 2]);
  out[23] = '-';
  *(uint16_t*)(&out[24]) = *(uint16_t*)(&kHexDigits[bytes[10] * 2]);
  *(uint16_t*)(&out[26]) = *(uint16_t*)(&kHexDigits[bytes[11] * 2]);
  *(uint16_t*)(&out[28]) = *(uint16_t*)(&kHexDigits[bytes[12] * 2]);
  *(uint16_t*)(&out[30]) = *(uint16_t*)(&kHexDigits[bytes[13] * 2]);
  *(uint16_t*)(&out[32]) = *(uint16_t*)(&kHexDigits[bytes[14] * 2]);
  *(uint16_t*)(&out[34]) = *(uint16_t*)(&kHexDigits[bytes[15] * 2]);
  out[36] = 0;
#pragma GCC diagnostic pop
}

errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t alloc,
                        a0_packet_t* out) {
  bool has_id = false;

  // TODO: Verify at most one id.
  for (size_t i = 0; i < num_headers; i++) {
    if (!strcmp(kIdKey, (char*)headers[i].key)) {
      has_id = true;
      break;
    }
  }

  // Alloc out space.
  {
    size_t size = sizeof(size_t);  // Num headers.
    if (!has_id) {
      size += 2 * sizeof(size_t);  // Id offsets, if not already in headers.
    }
    size += 2 * num_headers * sizeof(size_t);  // Header offsets.
    size += sizeof(size_t);                    // Payload offset.
    if (!has_id) {
      size += sizeof(kIdKey) + kUuidSize;  // Id content, if not already in headers.
    }
    for (size_t i = 0; i < num_headers; i++) {
      size += strlen(headers[i].key) + 1;  // Key content.
      size += strlen(headers[i].val) + 1;  // Val content.
    }
    size += payload.size;

    alloc.fn(alloc.user_data, size, out);
  }

  size_t total_headers = num_headers;
  if (!has_id) {
    total_headers++;
  }

  size_t off = 0;

  // Number of headers.
  memcpy(out->ptr + off, &total_headers, sizeof(size_t));
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * total_headers * sizeof(size_t) + sizeof(size_t);

  // Add an id if needed.

  if (!has_id) {
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
  }

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
    expanded_size += sizeof(size_t) + strlen(headers[i].key) + 1 + sizeof(size_t) + strlen(headers[i].val) + 1;
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
