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

// A0_STATIC_INLINE
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
  memcpy(out->ptr + off, &total_headers, sizeof(size_t));
  off += sizeof(size_t);

  size_t idx_off = off;
  off += 2 * total_headers * sizeof(size_t) + sizeof(size_t);

  // Add special headers first.

  // Uid key offset.
  memcpy(out->ptr + idx_off, &off, sizeof(size_t));
  idx_off += sizeof(size_t);

  // Uid key content.
  memcpy(out->ptr + off, id_key, strlen(id_key));
  off += strlen(id_key);

  // Uid val offset.
  memcpy(out->ptr + idx_off, &off, sizeof(size_t));
  idx_off += sizeof(size_t);

  // Uid val content.
  memcpy(out->ptr + off, &uid_val, sizeof(a0_packet_id_t));
  off += sizeof(a0_packet_id_t);

  // For each header.
  for (size_t i = 0; i < num_headers; i++) {
    // Header key offset.
    memcpy(out->ptr + idx_off, &off, sizeof(size_t));
    idx_off += sizeof(size_t);

    // Header key content.
    if (headers[i].key.size) {
      memcpy(out->ptr + off, headers[i].key.ptr, headers[i].key.size);
      off += headers[i].key.size;
    }

    // Header val offset.
    memcpy(out->ptr + idx_off, &off, sizeof(size_t));
    idx_off += sizeof(size_t);

    // Header val content.
    if (headers[i].val.size) {
      memcpy(out->ptr + off, headers[i].val.ptr, headers[i].val.size);
      off += headers[i].val.size;
    }
  }

  memcpy(out->ptr + idx_off, &off, sizeof(size_t));

  // Payload.
  if (payload.size) {
    memcpy(out->ptr + off, payload.ptr, payload.size);
  }

  return A0_OK;
}

errno_t a0_packet_add_headers(size_t num_headers,
                              a0_packet_header_t* headers,
                              a0_packet_t in,
                              a0_alloc_t alloc,
                              a0_packet_t* out) {
  size_t expanded_size = in.size;
  for (size_t i = 0; i < num_headers; i++) {
    expanded_size += sizeof(size_t) + headers[i].key.size + sizeof(size_t) + headers[i].val.size;
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

    memcpy(out->ptr + woff, headers[i].key.ptr, headers[i].key.size);
    woff += headers[i].key.size;

    memcpy(out->ptr + idx_woff, &woff, sizeof(size_t));
    idx_woff += sizeof(size_t);

    memcpy(out->ptr + woff, headers[i].val.ptr, headers[i].val.size);
    woff += headers[i].val.size;
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