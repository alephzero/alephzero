#include <a0/packet.h>

#include <stdio.h>
#include <string.h>

errno_t a0_packet_num_headers(a0_packet_t pkt, size_t* out) {
  *out = *(size_t*)pkt.ptr;
  return A0_OK;
}

errno_t a0_packet_header(a0_packet_t pkt, size_t hdr_idx, a0_packet_header_t* out) {
  size_t key_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 0) * sizeof(size_t)));
  size_t val_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 1) * sizeof(size_t)));
  size_t next_off = *(size_t*)(pkt.ptr + (sizeof(size_t) + (2 * hdr_idx + 2) * sizeof(size_t)));

  out->key.size = val_off - key_off;
  out->key.ptr = pkt.ptr + key_off;
  out->val.size = next_off - val_off;
  out->val.ptr = pkt.ptr + val_off;

  return A0_OK;
}

errno_t a0_packet_payload(a0_packet_t pkt, a0_buf_t* out) {
  size_t num_header = *(size_t*)pkt.ptr;
  size_t payload_off = *(size_t*)(pkt.ptr + sizeof(size_t) + (2 * num_header) * sizeof(size_t));

  out->size = pkt.size - payload_off;
  out->ptr = pkt.ptr + payload_off;

  return A0_OK;
}

errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t alloc,
                        a0_packet_t* out) {
  // Alloc out space.
  {
    size_t size = sizeof(size_t);              // Num headers.
    size += 2 * num_headers * sizeof(size_t);  // Header offsets.
    size += sizeof(size_t);                    // Payload offset.
    for (size_t i = 0; i < num_headers; i++) {
      size += headers[i].key.size;  // Key content.
      size += headers[i].val.size;  // Val content.
    }
    size += payload.size;

    alloc.fn(alloc.user_data, size, out);
  }

  size_t off = 0;

  // Number of headers.
  *(size_t*)(out->ptr + off) = num_headers;
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * num_headers * sizeof(size_t) + sizeof(size_t);

  // For each header.
  for (size_t i = 0; i < num_headers; i++) {
    // Header key offset.
    *(size_t*)(out->ptr + idx_off) = off;
    idx_off += sizeof(size_t);

    // Header key content.
    if (headers[i].key.size) {
      memcpy(out->ptr + off, headers[i].key.ptr, headers[i].key.size);
      off += headers[i].key.size;
    }

    // Header val offset.
    *(size_t*)(out->ptr + idx_off) = off;
    idx_off += sizeof(size_t);

    // Header val content.
    if (headers[i].val.size) {
      memcpy(out->ptr + off, headers[i].val.ptr, headers[i].val.size);
      off += headers[i].val.size;
    }
  }

  *(size_t*)(out->ptr + idx_off) = off;

  // Payload.
  if (payload.size) {
    memcpy(out->ptr + off, payload.ptr, payload.size);
  }

  return A0_OK;
}
