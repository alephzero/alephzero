#ifndef A0_RPC_H
#define A0_RPC_H

#include <a0/common.h>
#include <a0/packet.h>
#include <a0/stream.h>

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

errno_t a0_rpc_server_init_unmanaged(a0_rpc_server_t*,
                                     a0_shmobj_t,
                                     a0_alloc_t,
                                     a0_packet_callback_t onrequest,
                                     a0_packet_id_callback_t oncancel);
errno_t a0_rpc_server_close(a0_rpc_server_t*, a0_callback_t);
errno_t a0_rpc_server_await_close(a0_rpc_server_t*);
// The first packet, `req` is the one that triggered the onrequest callback.
// The second packet, `resp` is the one you wish to respond with.
// Note: do NOT respond with the request packet. The ids MUST be unique!
errno_t a0_rpc_reply(a0_rpc_server_t*, a0_packet_id_t req, a0_packet_t resp);

////////////
// Client //
////////////

typedef struct a0_rpc_client_impl_s a0_rpc_client_impl_t;

typedef struct a0_rpc_client_s {
  a0_rpc_client_impl_t* _impl;
} a0_rpc_client_t;

errno_t a0_rpc_client_init_unmanaged(a0_rpc_client_t*, a0_shmobj_t, a0_alloc_t);
errno_t a0_rpc_client_close(a0_rpc_client_t*, a0_callback_t);
errno_t a0_rpc_client_await_close(a0_rpc_client_t*);
errno_t a0_rpc_send(a0_rpc_client_t*, a0_packet_t, a0_packet_callback_t);
// Note: use the same packet that was provided to a0_rpc_send.
errno_t a0_rpc_cancel(a0_rpc_client_t*, a0_packet_id_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_RPC_H
