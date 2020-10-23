/**
 * \file packet.h
 * \rst
 *
 * What is a packet
 * ----------------
 *
 * | A simple container with the following elements: **ID**, **Headers**, **Payload**.
 * | Capable of being serialized and deserialed.
 *
 * ID
 * --
 *
 * | Unique UUID associated with the packet.
 * | Provided on construction and immutable.
 *
 * Headers
 * -------
 *
 *  Headers are a multipmap of utf8 key-value pairs.
 *
 *  Keys starting with **a0_** are reserved for AlephZero internals.
 *  Among them are:
 *
 *  * **a0_deps**:
 *    The **ID** of dependent packets.
 *    May be used as a key multiple times.
 *  * **a0_time_mono**:
 *    Monotonic/steady clock value.
 *    See :doc:`time` for more info.
 *  * **a0_time_wall**:
 *    Wall/system clock value (in RFC3339 / ISO8601 format).
 *    See :doc:`time` for more info.
 *  * **a0_transport_seq**:
 *    Sequence number among all packets in the transport.
 *  * **a0_publisher_seq**:
 *    Sequence number from the publisher.
 *  * **a0_publisher_id**:
 *    UUID of the publisher.
 *  * **...**
 *
 *  .. note::
 *
 *    Header keys & values are c-strings and include a null terminator.
 *
 * Payload
 * -------
 *
 * Arbitrary binary string.
 *
 * Serialization Format
 * --------------------
 *
 *  The serialized form has four parts:
 *
 *  * Packet id.
 *  * Index.
 *  * Header contents.
 *  * Payload content.
 *
 *  The index is added for O(1) lookup of headers and the payload.
 *
 *  +-------------------------------+
 *  | ID (a0_uuid_t)                |
 *  +-------------------------------+
 *  | num headers (size_t)          |
 *  +-------------------------------+
 *  | offset for hdr 0 key (size_t) |
 *  +-------------------------------+
 *  | offset for hdr 0 val (size_t) |
 *  +-------------------------------+
 *  |   .   .   .   .   .   .   .   |
 *  +-------------------------------+
 *  |   .   .   .   .   .   .   .   |
 *  +-------------------------------+
 *  | offset for hdr N key (size_t) |
 *  +-------------------------------+
 *  | offset for hdr N val (size_t) |
 *  +-------------------------------+
 *  | offset for payload (size_t)   |
 *  +-------------------------------+
 *  | hdr 0 key content             |
 *  +-------------------------------+
 *  | hdr 0 val content             |
 *  +-------------------------------+
 *  |   .   .   .   .   .   .   .   |
 *  +-------------------------------+
 *  |   .   .   .   .   .   .   .   |
 *  +-------------------------------+
 *  | hdr N key content             |
 *  +-------------------------------+
 *  | hdr N val content             |
 *  +-------------------------------+
 *  | payload content               |
 *  +-------------------------------+
 *
 * \endrst
 */

#ifndef A0_PACKET_H
#define A0_PACKET_H

#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/uuid.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup PACKET
 *  @{
 */

/// A single packet header.
typedef struct a0_packet_header_s {
  /// UTF-8 key.
  const char* key;
  /// UTF-8 value.
  const char* val;
} a0_packet_header_t;

typedef struct a0_packet_headers_block_s a0_packet_headers_block_t;

/**
 * A headers block contains a list of headers, along with an optional pointer to the next block.
 *
 * https://en.wikipedia.org/wiki/Unrolled_linked_list
 *
 * This is meant to make it easier for abstractions to add additional headers without allocating
 * heap space.
 *
 * \rst
 * .. code-block:: cpp
 *
 *     void foo(a0_packet_headers_block_t* caller_headers) {
 *       a0_packet_headers_block_t all_headers;
 *       all_headers.headers = additional_headers;
 *       all_headers.size = num_additional_headers;
 *       all_headers.next_block = caller_headers;
 *       bar(&all_headers);
 *     }
 * \endrst
 */
struct a0_packet_headers_block_s {
  /// Pointer to a contiguous array of headers.
  a0_packet_header_t* headers;
  /// Number of headers in the contiguous array.
  size_t size;
  /// Pointer to the next block.
  a0_packet_headers_block_t* next_block;
};

typedef struct a0_packet_s {
  /// Unique identifier for the packet.
  a0_uuid_t id;
  /// Packet headers.
  a0_packet_headers_block_t headers_block;
  /// Packet payload.
  a0_buf_t payload;
} a0_packet_t;

// The following are special keys.
// The returned buffers should not be cleaned up.

/// Packet header key used to annotate a dependence on another packet.
///
/// The value should be a packet id.
extern const char* A0_PACKET_DEP_KEY;

// Callback definition where packet is the only argument.

typedef struct a0_packet_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t);
} a0_packet_callback_t;

typedef struct a0_packet_header_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_header_t);
} a0_packet_header_callback_t;

typedef struct a0_packet_id_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_uuid_t);
} a0_packet_id_callback_t;

/// Initializes a packet. This includes setting the id.
errno_t a0_packet_init(a0_packet_t*);

/// Various computed stats of a given packet.
typedef struct a0_packet_stats_s {
  size_t num_hdrs;
  size_t content_size;
  size_t serial_size;
} a0_packet_stats_t;

/// Compute packet statistics.
errno_t a0_packet_stats(a0_packet_t, a0_packet_stats_t*);

/// Executes the given callback on all headers.
///
/// This includes headers across blocks.
errno_t a0_packet_for_each_header(a0_packet_headers_block_t, a0_packet_header_callback_t);

/// Serializes the packet to the allocated location.
///
/// **Note**: the header order will NOT be retained.
errno_t a0_packet_serialize(a0_packet_t, a0_alloc_t, a0_buf_t* out);

/// Deserializes the buffer into a packet.
///
/// The alloc is only used for the header pointers, not the contents.
///
/// The content will point into the buffer.
errno_t a0_packet_deserialize(a0_buf_t, a0_alloc_t, a0_packet_t* out);

/// Deep copies the packet contents.
errno_t a0_packet_deep_copy(a0_packet_t, a0_alloc_t, a0_packet_t* out);

/// Deallocates a packet that was previously allocated with the given alloc.
errno_t a0_packet_dealloc(a0_packet_t, a0_alloc_t);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_PACKET_H
