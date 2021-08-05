/**
 * \file writer.h
 * \rst
 *
 * Writer
 * ------
 *
 * A writer writes packets to a given arena using the AlephZero transport.
 *
 * \endrst
 */

#ifndef A0_WRITER_H
#define A0_WRITER_H

#include <a0/arena.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/writer_middleware.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup WRITER
 *  @{
 */

struct a0_writer_s {
  a0_writer_middleware_t _action;
  a0_writer_t* _next;
};

/// Initializes a writer.
errno_t a0_writer_init(a0_writer_t*, a0_arena_t);
/// Closes the given writer.
errno_t a0_writer_close(a0_writer_t*);
/// Serializes the given packet into the writer's arena.
errno_t a0_writer_write(a0_writer_t*, a0_packet_t);

/// ...
errno_t a0_writer_push(a0_writer_t*, a0_writer_middleware_t);

/**
 * Wraps a writer with a middleware as a new writer.
 *
 * The middleware is owned by the new writer and will be closed when the new writer is closed.
 *
 * The new writer does NOT own the old writer. The old writer may be reused.
 * The caller is responsible for closing the old writer AFTER the new writer is closed.
 */
errno_t a0_writer_wrap(a0_writer_t* in, a0_writer_middleware_t, a0_writer_t* out);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_WRITER_H
