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

typedef struct a0_rpc_request_s {
  // Note: do NOT reply with this packet. The id is not unique!
  a0_packet_t pkt;
} a0_rpc_request_t;

typedef struct a0_rpc_server_onrequest_s {
  void* user_data;
  void (*fn)(void* user_data, a0_rpc_request_t);
} a0_rpc_server_onrequest_t;

errno_t a0_rpc_server_init(a0_rpc_server_t*,
                           a0_shmobj_t request_shmobj,
                           a0_shmobj_t response_shmobj,
                           a0_alloc_t,
                           a0_rpc_server_onrequest_t);
errno_t a0_rpc_server_close(a0_rpc_server_t*, a0_callback_t);
errno_t a0_rpc_reply(a0_rpc_server_t*, a0_rpc_request_t, a0_packet_t);

////////////
// Client //
////////////

typedef struct a0_rpc_client_impl_s a0_rpc_client_impl_t;

typedef struct a0_rpc_client_s {
  a0_rpc_client_impl_t* _impl;
} a0_rpc_client_t;

errno_t a0_rpc_client_init(a0_rpc_client_t*,
                           a0_shmobj_t request_shmobj,
                           a0_shmobj_t response_shmobj,
                           a0_alloc_t);
errno_t a0_rpc_client_close(a0_rpc_client_t*, a0_callback_t);
errno_t a0_rpc_send(a0_rpc_client_t*, a0_packet_t, a0_packet_callback_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_RPC_H
