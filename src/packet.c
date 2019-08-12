#include <a0/packet.h>

#include <a0/internal/macros.h>
#include <a0/internal/rand.h>

#include <string.h>

static const char id_key[] = "a0_id";

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

errno_t a0_packet_id(a0_packet_t pkt, a0_packet_id_t* out) {
  size_t num_hdrs = 0;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_num_headers(pkt, &num_hdrs));
  for (size_t i = 0; i < num_hdrs; i++) {
    a0_packet_header_t hdr;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_header(pkt, i, &hdr));
    if (!strncmp(id_key, (char*)hdr.key.ptr, strlen(id_key))) {
      // Note: use memcpy, because memory in pkt hdrs is not aligned.
      memcpy(out, hdr.val.ptr, sizeof(a0_packet_id_t));
      return A0_OK;
    }
  }
  return EINVAL;
}

A0_STATIC_INLINE
a0_packet_id_t uuidv4() {
  a0_packet_id_t id = a0_mrand48();
  id |= (a0_packet_id_t)((a0_mrand48() & 0xFFFF0FFF) | 0x00004000) << 32;
  id |= (a0_packet_id_t)((a0_mrand48() & 0x3FFFFFFF) | 0x80000000) << 64;
  id |= (a0_packet_id_t)(a0_mrand48()) << 96;
  return id;
}

errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t alloc,
                        a0_packet_t* out) {
  // Add in a uuid.
  static const size_t extra_headers = 1;
  a0_packet_id_t uid_val = uuidv4();

  size_t total_headers = num_headers + extra_headers;

  // Alloc out space.
  {
    size_t size = sizeof(size_t);                // Num headers.
    size += 2 * total_headers * sizeof(size_t);  // Header offsets.
    size += sizeof(size_t);                      // Payload offset.
    size += strlen(id_key);                      // Uid key.
    size += sizeof(uid_val);                     // Uid val.
    for (size_t i = 0; i < num_headers; i++) {
      size += headers[i].key.size;  // Key content.
      size += headers[i].val.size;  // Val content.
    }
    size += payload.size;

    alloc.fn(alloc.user_data, size, out);
  }

  size_t off = 0;

  // Number of headers.
  *(size_t*)(out->ptr + off) = total_headers;
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * total_headers * sizeof(size_t) + sizeof(size_t);

  // Add special headers first.

  // Uid key offset.
  *(size_t*)(out->ptr + idx_off) = off;
  idx_off += sizeof(size_t);

  // Uid key content.
  memcpy(out->ptr + off, id_key, strlen(id_key));
  off += strlen(id_key);

  // Uid val offset.
  *(size_t*)(out->ptr + idx_off) = off;
  idx_off += sizeof(size_t);

  // Uid val content.
  memcpy(out->ptr + off, &uid_val, sizeof(a0_packet_id_t));
  off += sizeof(a0_packet_id_t);

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
