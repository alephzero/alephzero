#ifndef A0_PRPC_H
#define A0_PRPC_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/packet.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

////////////
// Server //
////////////

typedef struct a0_prpc_server_impl_s a0_prpc_server_impl_t;

typedef struct a0_prpc_server_s {
  a0_prpc_server_impl_t* _impl;
} a0_prpc_server_t;

typedef struct a0_prpc_connection_s {
  a0_prpc_server_t* server;
  a0_packet_t pkt;
} a0_prpc_connection_t;

typedef struct a0_prpc_connection_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_prpc_connection_t);
} a0_prpc_connection_callback_t;

errno_t a0_prpc_server_init(a0_prpc_server_t*,
                            a0_arena_t arena,
                            a0_alloc_t,
                            a0_prpc_connection_callback_t onconnect,
                            a0_packet_id_callback_t oncancel);
errno_t a0_prpc_server_close(a0_prpc_server_t*);
errno_t a0_prpc_server_async_close(a0_prpc_server_t*, a0_callback_t);
// Note: do NOT respond with the request packet. The ids MUST be unique!
errno_t a0_prpc_send(a0_prpc_connection_t, a0_packet_t, bool done);

////////////
// Client //
////////////

typedef struct a0_prpc_client_impl_s a0_prpc_client_impl_t;

typedef struct a0_prpc_client_s {
  a0_prpc_client_impl_t* _impl;
} a0_prpc_client_t;

typedef struct a0_prpc_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t, bool done);
} a0_prpc_callback_t;

errno_t a0_prpc_client_init(a0_prpc_client_t*, a0_arena_t arena, a0_alloc_t);
errno_t a0_prpc_client_close(a0_prpc_client_t*);
errno_t a0_prpc_client_async_close(a0_prpc_client_t*, a0_callback_t);
errno_t a0_prpc_connect(a0_prpc_client_t*, a0_packet_t, a0_prpc_callback_t);
// Note: use the same packet that was provided to a0_prpc_connect.
errno_t a0_prpc_cancel(a0_prpc_client_t*, const a0_uuid_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_PRPC_H
