#ifndef A0_INTERNAL_PACKET_TOOLS_H
#define A0_INTERNAL_PACKET_TOOLS_H

#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

// The packet content will be copied to an alloc-ed location.
// Note: the allocated space cannot overlap with the input pkt.
errno_t a0_packet_copy_with_additional_headers(size_t num_headers,
                                               a0_packet_header_t* headers,
                                               a0_packet_t in,
                                               a0_alloc_t,
                                               a0_packet_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_INTERNAL_PACKET_TOOLS_H
