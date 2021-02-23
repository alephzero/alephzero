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
  a0_writer_t* _next_writer;
};

/// Initializes a writer.
errno_t a0_writer_init(a0_writer_t*, a0_arena_t);
/// Closes the given writer.
errno_t a0_writer_close(a0_writer_t*);
/// Serializes the given packet into the writer's arena.
errno_t a0_writer_write(a0_writer_t*, a0_packet_t);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_WRITER_H
