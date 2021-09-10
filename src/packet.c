#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/uuid.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "err_macro.h"

const char* A0_PACKET_DEP_KEY = "a0_dep";

a0_err_t a0_packet_init(a0_packet_t* pkt) {
  memset(pkt, 0, sizeof(a0_packet_t));
  a0_uuidv4(pkt->id);
  return A0_OK;
}

a0_err_t a0_packet_stats(a0_packet_t pkt, a0_packet_stats_t* stats) {
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

a0_err_t a0_packet_header_iterator_init(a0_packet_header_iterator_t* iter, a0_packet_t* pkt) {
  *iter = (a0_packet_header_iterator_t){&pkt->headers_block, 0};
  return A0_OK;
}

a0_err_t a0_packet_header_iterator_next(a0_packet_header_iterator_t* iter,
                                        a0_packet_header_t* out) {
  while (iter->_block && iter->_idx >= iter->_block->size) {
    *iter = (a0_packet_header_iterator_t){iter->_block->next_block, 0};
  }

  if (!iter->_block) {
    return A0_ERR_ITER_DONE;
  }

  *out = iter->_block->headers[iter->_idx++];
  return A0_OK;
}

a0_err_t a0_packet_header_iterator_next_match(a0_packet_header_iterator_t* iter,
                                              const char* key,
                                              a0_packet_header_t* out) {
  do {
    A0_RETURN_ERR_ON_ERR(a0_packet_header_iterator_next(iter, out));
  } while (strcmp(key, out->key) != 0);
  return A0_OK;
}

a0_err_t a0_packet_serialize(a0_packet_t pkt, a0_alloc_t alloc, a0_flat_packet_t* out_fpkt) {
  a0_buf_t unused_out;
  a0_buf_t* out = &unused_out;
  if (out_fpkt) {
    out = &out_fpkt->buf;
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
  if (pkt.payload.size) {
    memcpy(out->ptr + off, pkt.payload.ptr, pkt.payload.size);
  }

  return A0_OK;
}

a0_err_t a0_packet_deserialize(a0_flat_packet_t fpkt, a0_alloc_t alloc, a0_packet_t* out_pkt, a0_buf_t* out_buf) {
  a0_buf_t in = fpkt.buf;
  memcpy(out_pkt->id, in.ptr, sizeof(a0_uuid_t));

  a0_packet_stats_t stats;
  a0_flat_packet_stats(fpkt, &stats);

  a0_alloc(alloc, stats.num_hdrs * sizeof(a0_packet_header_t) + stats.content_size, out_buf);

  uint8_t* write_ptr = out_buf->ptr;
  out_pkt->headers_block.headers = (a0_packet_header_t*)write_ptr;
  out_pkt->headers_block.size = stats.num_hdrs;
  write_ptr += stats.num_hdrs * sizeof(a0_packet_header_t);

  size_t* hdr_idx_ptr = (size_t*)(in.ptr + sizeof(a0_uuid_t) + sizeof(size_t));
  for (size_t i = 0; i < stats.num_hdrs; i++) {
    size_t key_off;
    memcpy(&key_off, hdr_idx_ptr + (2 * i), sizeof(size_t));
    out_pkt->headers_block.headers[i].key = (char*)write_ptr;
    strcpy((char*)out_pkt->headers_block.headers[i].key, (char*)(in.ptr + key_off));
    write_ptr += strlen((char*)(in.ptr + key_off)) + 1;

    size_t val_off;
    memcpy(&val_off, hdr_idx_ptr + (2 * i + 1), sizeof(size_t));
    out_pkt->headers_block.headers[i].val = (char*)write_ptr;
    strcpy((char*)out_pkt->headers_block.headers[i].val, (char*)(in.ptr + val_off));
    write_ptr += strlen((char*)(in.ptr + val_off)) + 1;
  }
  out_pkt->headers_block.next_block = NULL;

  size_t payload_off;
  memcpy(&payload_off,
         in.ptr
             // ID.
             + sizeof(a0_uuid_t)
             // Num headers.
             + sizeof(size_t)
             // Header offsets.
             + (2 * stats.num_hdrs) * sizeof(size_t),
         sizeof(size_t));

  out_pkt->payload.size = in.size - payload_off;
  out_pkt->payload.ptr = write_ptr;
  memcpy(out_pkt->payload.ptr, in.ptr + payload_off, out_pkt->payload.size);

  return A0_OK;
}

a0_err_t a0_packet_deep_copy(a0_packet_t in, a0_alloc_t alloc, a0_packet_t* out_pkt, a0_buf_t* out_buf) {
  memcpy(out_pkt->id, in.id, sizeof(a0_uuid_t));

  a0_packet_stats_t stats;
  A0_RETURN_ERR_ON_ERR(a0_packet_stats(in, &stats));

  a0_alloc(alloc,
           stats.num_hdrs * sizeof(a0_packet_header_t) + stats.content_size,
           out_buf);

  out_pkt->headers_block.headers = (a0_packet_header_t*)out_buf->ptr;
  out_pkt->headers_block.size = stats.num_hdrs;
  out_pkt->headers_block.next_block = NULL;

  size_t off = stats.num_hdrs * sizeof(a0_packet_header_t);

  size_t hdr_idx = 0;
  for (a0_packet_headers_block_t* block = &in.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* in_hdr = &block->headers[i];
      a0_packet_header_t* out_hdr = &out_pkt->headers_block.headers[hdr_idx];

      out_hdr->key = (char*)(out_buf->ptr + off);
      strcpy((char*)out_hdr->key, in_hdr->key);
      off += strlen(in_hdr->key) + 1;

      out_hdr->val = (char*)(out_buf->ptr + off);
      strcpy((char*)out_hdr->val, in_hdr->val);
      off += strlen(in_hdr->val) + 1;

      hdr_idx++;
    }
  }

  out_pkt->payload = (a0_buf_t){
      .ptr = out_buf->ptr + off,
      .size = in.payload.size,
  };
  memcpy(out_pkt->payload.ptr, in.payload.ptr, in.payload.size);

  return A0_OK;
}

a0_err_t a0_flat_packet_stats(a0_flat_packet_t fpkt, a0_packet_stats_t* stats) {
  stats->serial_size = fpkt.buf.size;
  stats->num_hdrs = *(size_t*)(fpkt.buf.ptr + sizeof(a0_uuid_t));

  size_t content_off =
      // ID.
      sizeof(a0_uuid_t)
      // Num headers.
      + sizeof(size_t)
      // Header offsets.
      + (2 * stats->num_hdrs) * sizeof(size_t)
      // Payload offset.
      + sizeof(size_t);

  stats->content_size = fpkt.buf.size - content_off;

  return A0_OK;
}

a0_err_t a0_flat_packet_id(a0_flat_packet_t fpkt, a0_uuid_t** out) {
  *out = (a0_uuid_t*)fpkt.buf.ptr;
  return A0_OK;
}

a0_err_t a0_flat_packet_payload(a0_flat_packet_t fpkt, a0_buf_t* out) {
  size_t num_hdrs = *(size_t*)(fpkt.buf.ptr + sizeof(a0_uuid_t));

  size_t payload_off;
  memcpy(&payload_off,
         fpkt.buf.ptr
             // ID.
             + sizeof(a0_uuid_t)
             // Num headers.
             + sizeof(size_t)
             // Header offsets.
             + (2 * num_hdrs) * sizeof(size_t),
         sizeof(size_t));

  *out = (a0_buf_t){
      .ptr = fpkt.buf.ptr + payload_off,
      .size = fpkt.buf.size - payload_off,
  };

  return A0_OK;
}

a0_err_t a0_flat_packet_header(a0_flat_packet_t fpkt, size_t idx, a0_packet_header_t* out) {
  size_t num_hdrs = *(size_t*)(fpkt.buf.ptr + sizeof(a0_uuid_t));
  if (idx >= num_hdrs) {
    return A0_ERR_NOT_FOUND;
  }

  size_t* hdr_idx_ptr = (size_t*)(fpkt.buf.ptr + sizeof(a0_uuid_t) + sizeof(size_t));

  size_t key_off;
  memcpy(&key_off, hdr_idx_ptr + (2 * idx), sizeof(size_t));
  size_t val_off;
  memcpy(&val_off, hdr_idx_ptr + (2 * idx + 1), sizeof(size_t));

  *out = (a0_packet_header_t){
      .key = (char*)(fpkt.buf.ptr + key_off),
      .val = (char*)(fpkt.buf.ptr + val_off),
  };

  return A0_OK;
}

a0_err_t a0_flat_packet_header_iterator_init(a0_flat_packet_header_iterator_t* iter, a0_flat_packet_t* fpkt) {
  *iter = (a0_flat_packet_header_iterator_t){fpkt, 0};
  return A0_OK;
}

a0_err_t a0_flat_packet_header_iterator_next(a0_flat_packet_header_iterator_t* iter, a0_packet_header_t* out) {
  return a0_flat_packet_header(*iter->_fpkt, iter->_idx++, out);
}

a0_err_t a0_flat_packet_header_iterator_next_match(a0_flat_packet_header_iterator_t* iter, const char* key, a0_packet_header_t* out) {
  do {
    A0_RETURN_ERR_ON_ERR(a0_flat_packet_header_iterator_next(iter, out));
  } while (strcmp(key, out->key) != 0);
  return A0_OK;
}
