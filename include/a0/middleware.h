/**
 * \file middleware.h
 * \rst
 *
 * Middleware
 * -----------------
 *
 * Middleware is designed to intercept and modify packets before they are
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
 *    Adds a header with a transport-wide sequence number.
 *  * **add_standard_headers**:
 *    Collection of all standard middleware.
 *
 * \endrst
 */

#ifndef A0_MIDDLEWARE_H
#define A0_MIDDLEWARE_H

#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_writer_s a0_writer_t;

/** \addtogroup MIDDLEWARE
 *  @{
 */

/**
 * Middleware Chain is an internal structure used to help serially process
 * a series of middleware without the need for heap allocation.
 *
 * All successful middleware processes should complete by executing the next
 * middleware in the chain.
 *
 * Note: NOT INTENDED TO BE USED BY USERS DIRECTLY.
 */

typedef struct a0_middleware_chain_node_s {
  a0_writer_t* _curr;
  a0_writer_t* _head;
  a0_transport_writer_locked_t* _twl;
} a0_middleware_chain_node_t;

typedef struct a0_middleware_chain_s {
  a0_middleware_chain_node_t _node;
  a0_err_t (*_chain_fn)(a0_middleware_chain_node_t, a0_packet_t*);
} a0_middleware_chain_t;

/**
 * Runs the next middleware in the chain.
 *
 * This is intended to be the last line in the middleware implementation.
 */
A0_STATIC_INLINE
a0_err_t a0_middleware_chain(a0_middleware_chain_t chain, a0_packet_t* pkt) {
  return chain._chain_fn(chain._node, pkt);
}

/**
 * Middleware is designed to intercept and modify packets before they are
 * serialized onto the arena.
 *
 * Each middleware instance should only be used for one writer. Closing the writer
 * will call the close method of the middleware.
 *
 * The "process" method is responsible for calling the next middleware in the chain.
 */
typedef struct a0_middleware_s {
  /// User data to be passed as context to other a0_middleware_t methods.
  void* user_data;
  /// Closes and frees all state associated with this middleware.
  a0_err_t (*close)(void* user_data);
  /// Processes a packet before forwarding it on to the next middleware in the chain.
  a0_err_t (*process)(void* user_data, a0_packet_t*, a0_middleware_chain_t);
  /// ...
  a0_err_t (*process_locked)(void* user_data, a0_transport_writer_locked_t*, a0_packet_t*, a0_middleware_chain_t);
} a0_middleware_t;

/**
 * Composes two middleware into a single middleware.
 *
 * The original middleware are owned by the new middleware.
 * They cannot be reused.
 * They will be closed when the new middleware is closed.
 */
a0_err_t a0_middleware_compose(a0_middleware_t, a0_middleware_t, a0_middleware_t* out);

/** @}*/

/** \addtogroup MIDDLEWARE_PROVIDED
 *  @{
 */

/// Creates a middleware that adds a mono timestamp header.
a0_middleware_t a0_add_time_mono_header();
/// Creates a middleware that adds a wall timestamp header.
a0_middleware_t a0_add_time_wall_header();
/// Creates a middleware that adds a writer id header.
a0_middleware_t a0_add_writer_id_header();
/// Creates a middleware that adds a writer sequence header.
a0_middleware_t a0_add_writer_seq_header();
/// Creates a middleware that adds a transport sequence header.
a0_middleware_t a0_add_transport_seq_header();
/// Creates a middleware that adds all standard headers.
a0_middleware_t a0_add_standard_headers();

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_MIDDLEWARE_H
