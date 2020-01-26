#ifndef A0_INTERNAL_PACKET_TOOLS_H
#define A0_INTERNAL_PACKET_TOOLS_H

#include <a0/packet.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

errno_t a0_packet_serialize(const a0_list_node_t* header_node,
                            const a0_list_node_t* payload_node,
                            a0_alloc_t,
                            a0_packet_t* out);

// The packet content will be copied to an alloc-ed location.
// Note: the allocated space cannot overlap with the input pkt.
errno_t a0_packet_copy_with_additional_headers(const a0_packet_header_list_t extra_header_list,
                                               const a0_packet_t in,
                                               a0_alloc_t,
                                               a0_packet_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_INTERNAL_PACKET_TOOLS_H
