#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/unused.h>
#include <a0/uuid.h>
#include <a0/middleware.h>

#include <stdint.h>
#include <stdlib.h>

#include "atomic.h"
#include "strconv.h"

A0_STATIC_INLINE
errno_t a0_add_time_mono_header_process_locked(void* data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
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
errno_t a0_add_time_wall_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
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
errno_t a0_add_writer_id_header_close(void* data) {
  free(data);
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_add_writer_id_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
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
errno_t a0_add_writer_seq_header_close(void* data) {
  free(data);
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_add_writer_seq_header_process(void* data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
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
errno_t a0_add_transport_seq_header_process_locked(void* data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
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
