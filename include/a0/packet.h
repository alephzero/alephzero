#ifndef A0_PACKET_H
#define A0_PACKET_H

#include <a0/common.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef a0_buf_t a0_packet_t;

typedef struct a0_packet_header_s {
  a0_buf_t key;
  a0_buf_t val;
} a0_packet_header_t;

errno_t a0_packet_num_headers(a0_packet_t, size_t* out);
// Note: out points to memory in the packet buffer.
errno_t a0_packet_header(a0_packet_t, size_t hdr_idx, a0_packet_header_t* out);
// Note: out points to memory in the packet buffer.
errno_t a0_packet_payload(a0_packet_t, a0_buf_t* out);

errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t,
                        a0_packet_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_PACKET_H
