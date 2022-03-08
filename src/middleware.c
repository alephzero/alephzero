#include <a0/buf.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/unused.h>
#include <a0/uuid.h>

#include <alloca.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <yyjson.h>

#include "atomic.h"
#include "err_macro.h"
#include "strconv.h"

A0_STATIC_INLINE
a0_err_t a0_add_time_mono_header_process_locked(void* data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(data);
  A0_MAYBE_UNUSED(tlk);

  a0_time_mono_t time_mono;
  a0_time_mono_now(&time_mono);

  char mono_str[20];
  a0_time_mono_str(time_mono, mono_str);

  a0_packet_header_t hdr = {A0_TIME_MONO, mono_str};
  a0_packet_headers_block_t prev_hdrs_blk = pkt->headers_block;

  pkt->headers_block = (a0_packet_headers_block_t){
      .headers = &hdr,
      .size = 1,
      .next_block = &prev_hdrs_blk,
  };

  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_add_time_mono_header() {
  return (a0_middleware_t){
      .user_data = NULL,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_add_time_mono_header_process_locked,
  };
}

A0_STATIC_INLINE
a0_err_t a0_add_time_wall_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(data);

  a0_time_wall_t time_wall;
  a0_time_wall_now(&time_wall);

  char wall_str[36];
  a0_time_wall_str(time_wall, wall_str);

  a0_packet_header_t hdr = {A0_TIME_WALL, wall_str};
  a0_packet_headers_block_t prev_hdrs_blk = pkt->headers_block;

  pkt->headers_block = (a0_packet_headers_block_t){
      .headers = &hdr,
      .size = 1,
      .next_block = &prev_hdrs_blk,
  };

  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_add_time_wall_header() {
  return (a0_middleware_t){
      .user_data = NULL,
      .close = NULL,
      .process = a0_add_time_wall_header_process,
      .process_locked = NULL,
  };
}

A0_STATIC_INLINE
void a0_add_writer_id_header_init(void** data) {
  a0_uuid_t* id = (a0_uuid_t*)malloc(sizeof(a0_uuid_t));
  a0_uuidv4(*id);
  *data = id;
}

A0_STATIC_INLINE
a0_err_t a0_add_writer_id_header_close(void* data) {
  free(data);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_add_writer_id_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  a0_packet_header_t hdr = {"a0_writer_id", (const char*)data};
  a0_packet_headers_block_t prev_hdrs_blk = pkt->headers_block;

  pkt->headers_block = (a0_packet_headers_block_t){
      .headers = &hdr,
      .size = 1,
      .next_block = &prev_hdrs_blk,
  };

  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_add_writer_id_header() {
  a0_middleware_t middleware;
  a0_add_writer_id_header_init(&middleware.user_data);
  middleware.close = a0_add_writer_id_header_close;
  middleware.process = a0_add_writer_id_header_process;
  middleware.process_locked = NULL;
  return middleware;
}

A0_STATIC_INLINE
void a0_add_writer_seq_header_init(void** data) {
  uint64_t* seq = (uint64_t*)malloc(sizeof(uint64_t));
  *seq = 0;
  *data = seq;
}

A0_STATIC_INLINE
a0_err_t a0_add_writer_seq_header_close(void* data) {
  free(data);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_add_writer_seq_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  uint64_t seq = a0_atomic_fetch_add((uint64_t*)data, 1);

  char seq_buf[20];
  char* seq_str;
  seq_buf[19] = '\0';
  a0_u64_to_str(seq, seq_buf, seq_buf + 19, &seq_str);

  a0_packet_header_t hdr = {"a0_writer_seq", (const char*)seq_str};
  a0_packet_headers_block_t prev_hdrs_blk = pkt->headers_block;

  pkt->headers_block = (a0_packet_headers_block_t){
      .headers = &hdr,
      .size = 1,
      .next_block = &prev_hdrs_blk,
  };

  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_add_writer_seq_header() {
  a0_middleware_t middleware;
  a0_add_writer_seq_header_init(&middleware.user_data);
  middleware.close = a0_add_writer_seq_header_close;
  middleware.process = a0_add_writer_seq_header_process;
  middleware.process_locked = NULL;
  return middleware;
}

A0_STATIC_INLINE
a0_err_t a0_add_transport_seq_header_process_locked(void* data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(data);

  uint64_t seq;
  a0_transport_seq_high(tlk, &seq);

  char seq_buf[20];
  char* seq_str;
  seq_buf[19] = '\0';
  a0_u64_to_str(seq, seq_buf, seq_buf + 19, &seq_str);

  a0_packet_header_t hdr = {"a0_transport_seq", seq_str};
  a0_packet_headers_block_t prev_hdrs_blk = pkt->headers_block;

  pkt->headers_block = (a0_packet_headers_block_t){
      .headers = &hdr,
      .size = 1,
      .next_block = &prev_hdrs_blk,
  };

  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_add_transport_seq_header() {
  return (a0_middleware_t){
      .user_data = NULL,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_add_transport_seq_header_process_locked,
  };
}

a0_middleware_t a0_add_standard_headers() {
  a0_middleware_t tmp0;
  a0_middleware_compose(
      a0_add_time_mono_header(),
      a0_add_time_wall_header(),
      &tmp0);

  a0_middleware_t tmp1;
  a0_middleware_compose(
      tmp0,
      a0_add_writer_id_header(),
      &tmp1);

  a0_middleware_t tmp2;
  a0_middleware_compose(
      tmp1,
      a0_add_writer_seq_header(),
      &tmp2);

  a0_middleware_t tmp3;
  a0_middleware_compose(
      tmp2,
      a0_add_transport_seq_header(),
      &tmp3);

  return tmp3;
}

A0_STATIC_INLINE
a0_err_t a0_write_if_empty_process_locked(void* data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  bool unused;
  bool* empty = (bool*)data;
  if (!data) {
    empty = &unused;
  }
  a0_transport_empty(tlk, empty);
  if (!*empty) {
    a0_transport_unlock(tlk);
    return A0_OK;
  }
  return a0_middleware_chain(chain, pkt);
}

a0_middleware_t a0_write_if_empty(bool* written) {
  return (a0_middleware_t){
      .user_data = written,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_write_if_empty_process_locked,
  };
}

A0_STATIC_INLINE
size_t a0_yyjson_read_flags() {
  return YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INF_AND_NAN;
}

A0_STATIC_INLINE
size_t a0_yyjson_write_flags() {
  return YYJSON_WRITE_ESCAPE_UNICODE | YYJSON_WRITE_ESCAPE_SLASHES;
}

A0_STATIC_INLINE
a0_err_t a0_json_mergepatch_process_locked_nonempty(
    a0_transport_locked_t tlk,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  // Grab the original json content from the most recent packet.
  a0_transport_jump_tail(tlk);
  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t flat_packet = {
      .buf = {frame.data, frame.hdr.data_size},
  };

  // Parse the original json.
  a0_buf_t original_payload;
  a0_flat_packet_payload(flat_packet, &original_payload);

  size_t original_size = yyjson_read_max_memory_usage(
      original_payload.size, a0_yyjson_read_flags());
  A0_ASSERT(original_size > 0);
  void* original_data = alloca(original_size);

  yyjson_alc original_alc;
  yyjson_alc_pool_init(
      &original_alc,
      original_data,
      original_size);

  yyjson_read_err read_err;
  yyjson_doc* original = yyjson_read_opts(
      (char*)original_payload.data,
      original_payload.size,
      a0_yyjson_read_flags(),
      &original_alc,
      &read_err);
  if (read_err.code) {
    a0_transport_unlock(tlk);
    return A0_MAKE_MSGERR("Failed to parse json: %s", read_err.msg);
  }

  // Parse the mergepatch json.
  size_t mergepatch_size = yyjson_read_max_memory_usage(
      pkt->payload.size, a0_yyjson_read_flags());
  A0_ASSERT(mergepatch_size > 0);
  void* mergepatch_data = alloca(mergepatch_size);

  yyjson_alc mergepatch_alc;
  yyjson_alc_pool_init(
      &mergepatch_alc,
      mergepatch_data,
      mergepatch_size);

  yyjson_doc* mergepatch = yyjson_read_opts(
      (char*)pkt->payload.data,
      pkt->payload.size,
      a0_yyjson_read_flags(),
      &mergepatch_alc,
      &read_err);
  if (read_err.code) {
    a0_transport_unlock(tlk);
    return A0_MAKE_MSGERR("Failed to parse json: %s", read_err.msg);
  }

  // Execute the mergepatch.
  yyjson_mut_doc* merged_doc = yyjson_mut_doc_new(NULL);

  merged_doc->root = yyjson_merge_patch(
      merged_doc,
      original->root,
      mergepatch->root);

  size_t result_max_size = original_size + mergepatch_size;
  void* result_data = alloca(result_max_size);

  yyjson_alc result_alc;
  yyjson_alc_pool_init(&result_alc, result_data, result_max_size);

  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_mut_write_opts(
      merged_doc,
      a0_yyjson_write_flags(),
      &result_alc,
      &size,
      &write_err);

  if (write_err.code) {
    yyjson_mut_doc_free(merged_doc);
    a0_transport_unlock(tlk);
    return A0_MAKE_MSGERR("Failed to serialize cfg: %s", write_err.msg);
  }

  // Update the packet payload to the mergepatch result.
  pkt->payload = (a0_buf_t){(uint8_t*)data, size};
  a0_err_t err = a0_middleware_chain(chain, pkt);

  yyjson_mut_doc_free(merged_doc);

  return err;
}

A0_STATIC_INLINE
a0_err_t a0_json_mergepatch_process_locked(
    void* user_data,
    a0_transport_locked_t tlk,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(user_data);
  bool empty;
  a0_transport_empty(tlk, &empty);

  if (empty) {
    return a0_middleware_chain(chain, pkt);
  }
  return a0_json_mergepatch_process_locked_nonempty(tlk, pkt, chain);
}

a0_middleware_t a0_json_mergepatch() {
  return (a0_middleware_t){
      .user_data = NULL,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_json_mergepatch_process_locked,
  };
}
