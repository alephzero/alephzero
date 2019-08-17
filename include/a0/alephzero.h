#ifndef A0_ALEPHZERO_H
#define A0_ALEPHZERO_H

#include <a0/pubsub.h>
#include <a0/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_alephzero_subscriber_def_s {
  const char* name;

  const char* container;
  const char* topic;
  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;
} a0_alephzero_subscriber_def_t;

typedef struct a0_alephzero_rpc_client_def_s {
  const char* name;

  const char* container;
  const char* topic;
} a0_alephzero_rpc_client_def_t;

typedef struct a0_alephzero_options_s {
  const char* container;

  size_t num_subscriber_defs;
  a0_alephzero_subscriber_def_t* subscriber_defs;

  size_t num_rpc_client_defs;
  a0_alephzero_rpc_client_def_t* rpc_client_defs;

//   size_t num_prpc_client_defs;
//   a0_alephzero_prpc_client_def_t* prpc_client_defs;

//   size_t num_session_client_defs;
//   a0_alephzero_session_client_def_t* session_client_defs;
} a0_alephzero_options_t;

typedef struct a0_alephzero_impl_s a0_alephzero_impl_t;

typedef struct a0_alephzero_s {
  a0_alephzero_impl_t* _impl;
} a0_alephzero_t;

errno_t a0_alephzero_init(a0_alephzero_t*);  // Loads options from env.
errno_t a0_alephzero_init_explicit(a0_alephzero_t*, a0_alephzero_options_t);
errno_t a0_alephzero_close(a0_alephzero_t*);

errno_t a0_publisher_init(a0_publisher_t*, a0_alephzero_t, const char* name);
errno_t a0_subscriber_sync_init(a0_subscriber_sync_t*, a0_alephzero_t, const char* name);
errno_t a0_subscriber_zero_copy_init(a0_subscriber_zero_copy_t*,
                                     a0_alephzero_t,
                                     const char* name,
                                     a0_zero_copy_callback_t);
errno_t a0_subscriber_init(a0_subscriber_t*,
                           a0_alephzero_t,
                           const char* name,
                           a0_alloc_t,
                           a0_packet_callback_t);

errno_t a0_rpc_server_init(a0_rpc_server_t*,
                           a0_alephzero_t,
                           const char* name,
                           a0_alloc_t,
                           a0_packet_callback_t onrequest,
                           a0_packet_callback_t oncancel);
errno_t a0_rpc_client_init(a0_rpc_client_t*, a0_alephzero_t, const char* name, a0_alloc_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_ALEPHZERO_H
