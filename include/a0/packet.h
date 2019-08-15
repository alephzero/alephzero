#ifndef A0_PACKET_H
#define A0_PACKET_H

#include <a0/common.h>

#ifdef __cplusplus
extern "C" {
#endif

// A packet is a simple data format that contains key-value headers and a payload.
//
// The header fields may be arbitrary byte arrays and key can be repeated.
//
// Header fields whose keys start with "a0_" are special cased.
// Among them are:
//   * "a0_id": The unique id of the packet.
//              This field will be auto-generated with a uuidv4 value, if not provided.
//              It is recommended to NOT provide this, and let the builder auto-generate it.
//   * "a0_deps": A repeated key referencing the unique ids of other packets.
//
// A packet is implemented as a flat buffer. The layout is described below.

typedef a0_buf_t a0_packet_t;

typedef struct a0_packet_header_s {
  a0_buf_t key;
  a0_buf_t val;
} a0_packet_header_t;

// The following are special keys.
// The returned buffers should not be cleaned up.

// No more than one id is allowed.
// One will be added automatically if not present when building.
a0_buf_t a0_packet_id_key();
a0_buf_t a0_packet_dep_key();

// The following are packet accessors.

// Get the number of headers in the packet.
errno_t a0_packet_num_headers(a0_packet_t, size_t* out);
// Get the packet header at index idx.
// Note: out points to memory in the packet buffer.
// Note: this is an O(1) operation.
errno_t a0_packet_header(a0_packet_t, size_t hdr_idx, a0_packet_header_t* out);
// Get the packet payload.
// Note: out points to memory in the packet buffer.
errno_t a0_packet_payload(a0_packet_t, a0_buf_t* out);
// Get the value for the given header key.
// Note: out points to memory in the packet buffer.
// Note: A naive implementation would be O(N). Maybe sort the headers and bisect?
errno_t a0_packet_find_header(a0_packet_t, a0_buf_t key, a0_buf_t* val_out);

// Helper method to get the packet id.
inline errno_t a0_packet_id(a0_packet_t pkt, a0_buf_t* id_out) {
  return a0_packet_find_header(pkt, a0_packet_id_key(), id_out);
}

// The following build or modify packets.

// Note: the header order will NOT be retained.
errno_t a0_packet_build(size_t num_headers,
                        a0_packet_header_t* headers,
                        a0_buf_t payload,
                        a0_alloc_t,
                        a0_packet_t* out);

// Callback definition where packet is the only argument.

typedef struct a0_packet_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t);
} a0_packet_callback_t;

// The format of a packet is described here.
// It is recommended to not worry about this too much, and just use a0_packet_build.
//
// A packet has a header region followed by a payload.
// The header has a lookup table followed by a number of key-value pairs.
// The lookup table is designed for O(1) lookup of headers and the payload.
//
// +-------------------------------+
// | num headers (size_t)          |
// +-------------------------------+
// | offset for hdr 0 key (size_t) |
// +-------------------------------+
// | offset for hdr 0 val (size_t) |
// +-------------------------------+
// |                               |
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// |                               |
// +-------------------------------+
// | offset for hdr N key (size_t) |
// +-------------------------------+
// | offset for hdr N val (size_t) |
// +-------------------------------+
// | offset for payload (size_t)   |
// +-------------------------------+
// | hdr 0 key content             |
// +-------------------------------+
// | hdr 0 val content             |
// +-------------------------------+
// |                               |
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// +-------------------------------+
// | hdr N key content             |
// +-------------------------------+
// | hdr N val content             |
// +-------------------------------+
// | payload content               |
// +-------------------------------+

#ifdef __cplusplus
}
#endif

#endif  // A0_PACKET_H
