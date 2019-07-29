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

errno_t a0_packet_build(a0_packet_builder_t builder, a0_alloc_t alloc, a0_packet_t* out) {
  // Alloc out space.
  {
    size_t size = sizeof(size_t);                        // Num headers.
    size += 2 * builder.num_headers * sizeof(size_t);   // Header offsets.
    size += sizeof(size_t);                              // Payload offset.
    for (int i = 0; i < builder.num_headers; i++) {
      size += builder.headers[i].key.size;              // Key content.
      size += builder.headers[i].val.size;              // Val content.
    }
    size += builder.payload.size;

    alloc.callback(alloc.user_data, size, out);
  }

  size_t off = 0;

  // Number of headers.
  *(size_t*)(out->ptr + off) = builder.num_headers;
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * builder.num_headers * sizeof(size_t) + sizeof(size_t);

  // For each header.
  for (size_t i = 0; i < builder.num_headers; i++) {
    // Header key offset.
    *(size_t*)(out->ptr + idx_off) = off;
    idx_off += sizeof(size_t);

    // Header key content.
    memcpy(out->ptr + off, builder.headers[i].key.ptr, builder.headers[i].key.size);
    off += builder.headers[i].key.size;

    // Header val offset.
    *(size_t*)(out->ptr + idx_off) = off;
    idx_off += sizeof(size_t);

    // Header val content.
    memcpy(out->ptr + off, builder.headers[i].val.ptr, builder.headers[i].val.size);
    off += builder.headers[i].val.size;
  }

  *(size_t*)(out->ptr + idx_off) = off;

  // Payload.
  memcpy(out->ptr + off, builder.payload.ptr, builder.payload.size);

  return A0_OK;
}
