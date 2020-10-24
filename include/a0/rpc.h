#ifndef A0_RPC_H
#define A0_RPC_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

////////////
// Server //
////////////

typedef struct a0_rpc_server_impl_s a0_rpc_server_impl_t;

typedef struct a0_rpc_server_s {
  a0_rpc_server_impl_t* _impl;
} a0_rpc_server_t;

typedef struct a0_rpc_request_s {
  a0_rpc_server_t* server;
  a0_packet_t pkt;
} a0_rpc_request_t;

typedef struct a0_rpc_request_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_rpc_request_t);
} a0_rpc_request_callback_t;

errno_t a0_rpc_server_init(a0_rpc_server_t*,
                           a0_arena_t,
                           a0_alloc_t,
                           a0_rpc_request_callback_t onrequest,
                           a0_packet_id_callback_t oncancel);
errno_t a0_rpc_server_close(a0_rpc_server_t*);
errno_t a0_rpc_server_async_close(a0_rpc_server_t*, a0_callback_t);

// Note: do NOT respond with the request packet. The ids MUST be unique!
errno_t a0_rpc_reply(a0_rpc_request_t, a0_packet_t resp);

////////////
// Client //
////////////

typedef struct a0_rpc_client_impl_s a0_rpc_client_impl_t;

typedef struct a0_rpc_client_s {
  a0_rpc_client_impl_t* _impl;
} a0_rpc_client_t;

errno_t a0_rpc_client_init(a0_rpc_client_t*, a0_arena_t, a0_alloc_t);
errno_t a0_rpc_client_close(a0_rpc_client_t*);
errno_t a0_rpc_client_async_close(a0_rpc_client_t*, a0_callback_t);
errno_t a0_rpc_send(a0_rpc_client_t*, a0_packet_t, a0_packet_callback_t);
// Note: use the same packet that was provided to a0_rpc_send.
errno_t a0_rpc_cancel(a0_rpc_client_t*, const a0_uuid_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_RPC_H
