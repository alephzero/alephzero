/**
 * \file writer_middleware.h
 * \rst
 *
 * Writer Middleware
 * -----------------
 *
 * Writer Middleware is designed to intercept and modify packets before they are
 * serialized onto the arena.
 *
 * Provided middleware include:
 *
 *  * **add_time_mono_header**:
 *    Adds a header with a mono timestamp.
 *    See :doc:`time` for more info.
 *  * **add_time_wall_header**:
 *    Adds a header with a wall timestamp.
 *    See :doc:`time` for more info.
 *  * **add_writer_id_header**:
 *    Adds a header with a unique id for the writer.
 *  * **add_writer_seq_header**:
 *    Adds a header with a write sequence number for the writer.
 *  * **add_transport_seq_header**:
 *    Not yet available. Adds a header with a transport-wide sequence number.
 *  * **add_standard_headers**:
 *    Collection of all standard middleware.
 *
 * \endrst
 */

#ifndef A0_WRITER_MIDDLEWARE_H
#define A0_WRITER_MIDDLEWARE_H

#include <a0/arena.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_writer_s a0_writer_t;

/** \addtogroup WRITER_MIDDLEWARE
 *  @{
 */

/**
 * Writer Middleware Chain is an internal structure used to help serially process
 * a series of middleware without the need for heap allocation.
 *
 * All successful middleware processes should complete by executing the next
 * middleware in the chain.
 *
 * Note: NOT INTENDED TO BE USED BY USERS DIRECTLY.
 */

typedef struct a0_writer_middleware_chain_node_s {
  a0_writer_t* _curr;
  a0_writer_t* _head;
  a0_locked_transport_t _tlk;
} a0_writer_middleware_chain_node_t;

typedef struct a0_writer_middleware_chain_s {
  a0_writer_middleware_chain_node_t _node;
  errno_t (*_chain_fn)(a0_writer_middleware_chain_node_t, a0_packet_t*);
} a0_writer_middleware_chain_t;

/**
 * Writer Middleware is designed to intercept and modify packets before they are
 * serialized onto the arena.
 *
 * Each middleware instance should only be used for one writer. Closing the writer
 * will call the close method of the middleware.
 *
 * The "process" method is responsible for calling the next middleware in the chain.
 */
typedef struct a0_writer_middleware_s {
  /// User data to be passed as context to other a0_writer_middleware_t methods.
  void* user_data;
  /// Closes and frees all state associated with this middleware.
  errno_t (*close)(void* user_data);
  /// Processes a packet before forwarding it on to the next middleware in the chain.
  errno_t (*process)(void* user_data, a0_packet_t*, a0_writer_middleware_chain_t);
  /// ...
  errno_t (*process_locked)(void* user_data, a0_locked_transport_t, a0_packet_t*, a0_writer_middleware_chain_t);
} a0_writer_middleware_t;

/**
 * Wraps a writer with a middleware as a new writer.
 *
 * The middleware is owned by the new writer and will be closed when the new writer is closed.
 *
 * The new writer does NOT own the old writer. The old writer may be reused.
 * The caller is responsible for closing the old writer AFTER the new writer is closed.
 */
errno_t a0_writer_wrap(a0_writer_t* in, a0_writer_middleware_t, a0_writer_t* out);

/**
 * Composes two middleware into a single middleware.
 *
 * The original middleware are owned by the new middleware.
 * They cannot be reused.
 * They will be closed when the new middleware is closed.
 */
errno_t a0_writer_middleware_compose(a0_writer_middleware_t, a0_writer_middleware_t, a0_writer_middleware_t* out);

/**
 * Runs the next middleware in the chain.
 *
 * This is intended to be the last line in the middleware implementation.
 */
A0_STATIC_INLINE
errno_t a0_writer_middleware_chain(a0_writer_middleware_chain_t chain, a0_packet_t* pkt) {
  return chain._chain_fn(chain._node, pkt);
}

/** @}*/

/** \addtogroup WRITER_MIDDLEWARE_PROVIDED
 *  @{
 */

/// Creates a middleware that adds a mono timestamp header.
a0_writer_middleware_t a0_writer_middleware_add_time_mono_header();
/// Creates a middleware that adds a wall timestamp header.
a0_writer_middleware_t a0_writer_middleware_add_time_wall_header();
/// Creates a middleware that adds a writer id header.
a0_writer_middleware_t a0_writer_middleware_add_writer_id_header();
/// Creates a middleware that adds a writer sequence header.
a0_writer_middleware_t a0_writer_middleware_add_writer_seq_header();
/// ...
a0_writer_middleware_t a0_writer_middleware_add_transport_seq_header();
/// Creates a middleware that adds all standard headers.
a0_writer_middleware_t a0_writer_middleware_add_standard_headers();

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_WRITER_MIDDLEWARE_H
