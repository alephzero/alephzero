#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/uuid.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "err_util.h"

const char* A0_PACKET_DEP_KEY = "a0_dep";

errno_t a0_packet_init(a0_packet_t* pkt) {
  memset(pkt, 0, sizeof(a0_packet_t));
  a0_uuidv4(pkt->id);
  return A0_OK;
}

errno_t a0_packet_stats(a0_packet_t pkt, a0_packet_stats_t* stats) {
  stats->num_hdrs = 0;
  for (a0_packet_headers_block_t* block = &pkt.headers_block;
       block;
       block = block->next_block) {
    stats->num_hdrs += block->size;
  }

  stats->content_size = 0;
  for (a0_packet_headers_block_t* block = &pkt.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* hdr = &block->headers[i];
      stats->content_size += strlen(hdr->key) + 1;  // Key content.
      stats->content_size += strlen(hdr->val) + 1;  // Val content.
    }
  }
  stats->content_size += pkt.payload.size;

  stats->serial_size = sizeof(a0_uuid_t)                         // ID.
                       + sizeof(size_t)                          // Num headers.
                       + 2 * (stats->num_hdrs) * sizeof(size_t)  // Header offsets.
                       + sizeof(size_t)                          // Payload offset.
                       + stats->content_size;                    // Content.

  return A0_OK;
}

errno_t a0_packet_for_each_header(a0_packet_headers_block_t headers_block,
                                  a0_packet_header_callback_t onheader) {
  for (a0_packet_headers_block_t* block = &headers_block; block; block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      onheader.fn(onheader.user_data, block->headers[i]);
    }
  }
  return A0_OK;
}

errno_t a0_packet_serialize(a0_packet_t pkt, a0_alloc_t alloc, a0_buf_t* out) {
  a0_buf_t unused_out;
  if (!out) {
    out = &unused_out;
  }

  a0_packet_stats_t stats;
  A0_RETURN_ERR_ON_ERR(a0_packet_stats(pkt, &stats));

  a0_alloc(alloc, stats.serial_size, out);

  // Write pointer into index.
  size_t idx_off = 0;

  // Write pointer into content.
  size_t off = sizeof(a0_uuid_t)                      // ID.
               + sizeof(size_t)                       // Num headers.
               + 2 * stats.num_hdrs * sizeof(size_t)  // Header offsets.
               + sizeof(size_t);                      // Payload offset.

  // ID.
  memcpy(out->ptr + idx_off, pkt.id, sizeof(a0_uuid_t));
  idx_off += sizeof(a0_uuid_t);

  // Number of headers.
  memcpy(out->ptr + idx_off, &stats.num_hdrs, sizeof(size_t));
  idx_off += sizeof(size_t);

  // For each header.
  for (a0_packet_headers_block_t* block = &pkt.headers_block;
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

  // Payload offset.
  memcpy(out->ptr + idx_off, &off, sizeof(size_t));

  // Payload content.
  memcpy(out->ptr + off, pkt.payload.ptr, pkt.payload.size);

  return A0_OK;
}

errno_t a0_packet_deserialize(a0_flat_packet_t fpkt, a0_alloc_t alloc, a0_packet_t* out) {
  memcpy(out->id, fpkt.ptr, sizeof(a0_uuid_t));

  a0_packet_stats_t stats;
  a0_flat_packet_stats(fpkt, &stats);

  a0_buf_t buf;
  a0_alloc(alloc, stats.num_hdrs * sizeof(a0_packet_header_t) + stats.content_size, &buf);

  uint8_t* write_ptr = buf.ptr;
  out->headers_block.headers = (a0_packet_header_t*)write_ptr;
  out->headers_block.size = stats.num_hdrs;
  write_ptr += stats.num_hdrs * sizeof(a0_packet_header_t);

  size_t* hdr_idx_ptr = (size_t*)(fpkt.ptr + sizeof(a0_uuid_t) + sizeof(size_t));
  for (size_t i = 0; i < stats.num_hdrs; i++) {
    size_t key_off;
    memcpy(&key_off, hdr_idx_ptr + (2 * i), sizeof(size_t));
    out->headers_block.headers[i].key = (char*)write_ptr;
    strcpy((char*)out->headers_block.headers[i].key, (char*)(fpkt.ptr + key_off));
    write_ptr += strlen((char*)(fpkt.ptr + key_off)) + 1;

    size_t val_off;
    memcpy(&val_off, hdr_idx_ptr + (2 * i + 1), sizeof(size_t));
    out->headers_block.headers[i].val = (char*)write_ptr;
    strcpy((char*)out->headers_block.headers[i].val, (char*)(fpkt.ptr + val_off));
    write_ptr += strlen((char*)(fpkt.ptr + val_off)) + 1;
  }
  out->headers_block.next_block = NULL;

  size_t payload_off;
  memcpy(&payload_off,
         fpkt.ptr
             // ID.
             + sizeof(a0_uuid_t)
             // Num headers.
             + sizeof(size_t)
             // Header offsets.
             + (2 * stats.num_hdrs) * sizeof(size_t),
         sizeof(size_t));

  out->payload.size = fpkt.size - payload_off;
  out->payload.ptr = write_ptr;
  memcpy(out->payload.ptr, fpkt.ptr + payload_off, out->payload.size);

  return A0_OK;
}

errno_t a0_packet_deep_copy(a0_packet_t in, a0_alloc_t alloc, a0_packet_t* out) {
  memcpy(out->id, in.id, sizeof(a0_uuid_t));

  a0_packet_stats_t stats;
  A0_RETURN_ERR_ON_ERR(a0_packet_stats(in, &stats));

  a0_buf_t space;
  a0_alloc(alloc,
           stats.num_hdrs * sizeof(a0_packet_header_t) + stats.content_size,
           &space);

  out->headers_block.headers = (a0_packet_header_t*)space.ptr;
  out->headers_block.size = stats.num_hdrs;
  out->headers_block.next_block = NULL;

  size_t off = stats.num_hdrs * sizeof(a0_packet_header_t);

  size_t hdr_idx = 0;
  for (a0_packet_headers_block_t* block = &in.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* in_hdr = &block->headers[i];
      a0_packet_header_t* out_hdr = &out->headers_block.headers[hdr_idx];

      out_hdr->key = (char*)(space.ptr + off);
      strcpy((char*)out_hdr->key, in_hdr->key);
      off += strlen(in_hdr->key) + 1;

      out_hdr->val = (char*)(space.ptr + off);
      strcpy((char*)out_hdr->val, in_hdr->val);
      off += strlen(in_hdr->val) + 1;

      hdr_idx++;
    }
  }

  out->payload = (a0_buf_t){
      .ptr = space.ptr + off,
      .size = in.payload.size,
  };
  memcpy(out->payload.ptr, in.payload.ptr, in.payload.size);

  return A0_OK;
}

errno_t a0_packet_dealloc(a0_packet_t pkt, a0_alloc_t alloc) {
  a0_packet_stats_t stats;
  A0_RETURN_ERR_ON_ERR(a0_packet_stats(pkt, &stats));

  a0_buf_t buf = {
      .ptr = (uint8_t*)pkt.headers_block.headers,
      .size = stats.num_hdrs * sizeof(a0_packet_header_t) + stats.content_size,
  };
  return a0_dealloc(alloc, buf);
}

errno_t a0_flat_packet_stats(a0_flat_packet_t fpkt, a0_packet_stats_t* stats) {
  stats->serial_size = fpkt.size;
  stats->num_hdrs = *(size_t*)(fpkt.ptr + sizeof(a0_uuid_t));

  size_t content_off =
      // ID.
      sizeof(a0_uuid_t)
      // Num headers.
      + sizeof(size_t)
      // Header offsets.
      + (2 * stats->num_hdrs) * sizeof(size_t)
      // Payload offset.
      + sizeof(size_t);

  stats->content_size = fpkt.size - content_off;

  return A0_OK;
}

errno_t a0_flat_packet_id(a0_flat_packet_t fpkt, a0_uuid_t** out) {
  *out = (a0_uuid_t*)fpkt.ptr;
  return A0_OK;
}

errno_t a0_flat_packet_payload(a0_flat_packet_t fpkt, a0_buf_t* out) {
  size_t num_hdrs = *(size_t*)(fpkt.ptr + sizeof(a0_uuid_t));

  size_t payload_off;
  memcpy(&payload_off,
         fpkt.ptr
             // ID.
             + sizeof(a0_uuid_t)
             // Num headers.
             + sizeof(size_t)
             // Header offsets.
             + (2 * num_hdrs) * sizeof(size_t),
         sizeof(size_t));

  *out = (a0_buf_t){
      .ptr = fpkt.ptr + payload_off,
      .size = fpkt.size - payload_off,
  };

  return A0_OK;
}

errno_t a0_flat_packet_header(a0_flat_packet_t fpkt, size_t idx, a0_packet_header_t* out) {
  size_t num_hdrs = *(size_t*)(fpkt.ptr + sizeof(a0_uuid_t));
  if (idx >= num_hdrs) {
    return EINVAL;
  }

  size_t* hdr_idx_ptr = (size_t*)(fpkt.ptr + sizeof(a0_uuid_t) + sizeof(size_t));

  size_t key_off;
  memcpy(&key_off, hdr_idx_ptr + (2 * idx), sizeof(size_t));
  size_t val_off;
  memcpy(&val_off, hdr_idx_ptr + (2 * idx + 1), sizeof(size_t));

  *out = (a0_packet_header_t){
      .key = (char*)(fpkt.ptr + key_off),
      .val = (char*)(fpkt.ptr + val_off),
  };

  return A0_OK;
}
