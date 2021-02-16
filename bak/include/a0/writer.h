#ifndef A0_WRITER_H
#define A0_WRITER_H

#include <a0/common.h>
#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_writer_impl_s a0_writer_impl_t;

typedef struct a0_writer_s {
  a0_writer_impl_t* _impl;
} a0_writer_t;

errno_t a0_writer_init(a0_writer_t*, a0_arena_t);
errno_t a0_writer_close(a0_writer_t*);
errno_t a0_writer_write(a0_writer_t*, a0_packet_t);

//////////////////////////////////
// Middleware Definitions Below //
//////////////////////////////////

typedef struct a0_writer_middleware_chain_s {
  void* data;
  errno_t (*chain_fn)(void* data, a0_packet_t);
} a0_writer_middleware_chain_t;

A0_STATIC_INLINE
errno_t a0_writer_middleware_chain(a0_writer_middleware_chain_t chain, a0_packet_t pkt) {
  return chain.chain_fn(chain.data, pkt);
}

// A middleware instance should only be used for one writer.
// The lifetime of a middleware is bound to that writer.
typedef struct a0_writer_middleware_s {
  void* user_data;
  errno_t (*close)(void* user_data);
  errno_t (*process)(void* user_data, a0_packet_t, a0_writer_middleware_chain_t);
} a0_writer_middleware_t;

// The middleware is owned by the output writer.
errno_t a0_writer_wrap(a0_writer_t* in, a0_writer_middleware_t, a0_writer_t* out);

// The input middleware are owned by the output middleware.
errno_t a0_writer_middleware_compose(a0_writer_middleware_t, a0_writer_middleware_t, a0_writer_middleware_t* out);

a0_writer_middleware_t a0_writer_middleware_add_time_mono_header();
a0_writer_middleware_t a0_writer_middleware_add_time_wall_header();
a0_writer_middleware_t a0_writer_middleware_add_writer_id_header();
a0_writer_middleware_t a0_writer_middleware_add_writer_seq_header();

// TODO(lshamis): Add
// a0_writer_middleware_t a0_writer_middleware_add_transport_seq_header();

a0_writer_middleware_t a0_writer_middleware_add_time_headers();
a0_writer_middleware_t a0_writer_middleware_add_id_headers();
a0_writer_middleware_t a0_writer_middleware_add_seq_headers();
a0_writer_middleware_t a0_writer_middleware_add_writer_headers();
a0_writer_middleware_t a0_writer_middleware_add_standard_headers();

#ifdef __cplusplus
}
#endif

#endif  // A0_WRITER_H
