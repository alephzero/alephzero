#ifndef A0_ALEPHZERO_H
#define A0_ALEPHZERO_H

#include <a0/pubsub.h>
#include <a0/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_alephzero_topic_map_s {
  const char* name;

  const char* container;
  const char* topic;
} a0_alephzero_topic_map_t;

typedef struct a0_alephzero_options_s {
  const char* container;

  size_t num_subscriber_maps;
  a0_alephzero_topic_map_t* subscriber_maps;

  size_t num_rpc_client_maps;
  a0_alephzero_topic_map_t* rpc_client_maps;
} a0_alephzero_options_t;

typedef struct a0_alephzero_impl_s a0_alephzero_impl_t;

typedef struct a0_alephzero_s {
  a0_alephzero_impl_t* _impl;
} a0_alephzero_t;

errno_t a0_alephzero_init(a0_alephzero_t*);  // Loads options from env.
errno_t a0_alephzero_init_explicit(a0_alephzero_t*, a0_alephzero_options_t);
errno_t a0_alephzero_close(a0_alephzero_t*);

errno_t a0_config_sync_init(a0_subscriber_sync_t*, a0_alephzero_t);
errno_t a0_config_init(a0_subscriber_sync_t*, a0_alephzero_t, a0_packet_callback_t);

errno_t a0_publisher_init(a0_publisher_t*, a0_alephzero_t, const char* name);
errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t*,
                                   a0_alephzero_t,
                                   const char* name,
                                   a0_subscriber_read_start_t,
                                   a0_subscriber_read_next_t);
errno_t a0_subscriber_sync_init(a0_subscriber_sync_t*,
                                a0_alephzero_t,
                                const char* name,
                                a0_subscriber_read_start_t,
                                a0_subscriber_read_next_t);
errno_t a0_subscriber_zc_init(a0_subscriber_zc_t*,
                              a0_alephzero_t,
                              const char* name,
                              a0_subscriber_read_start_t,
                              a0_subscriber_read_next_t,
                              a0_zero_copy_callback_t);
errno_t a0_subscriber_init(a0_subscriber_t*,
                           a0_alephzero_t,
                           const char* name,
                           a0_subscriber_read_start_t,
                           a0_subscriber_read_next_t,
                           a0_packet_callback_t);

errno_t a0_rpc_server_init(a0_rpc_server_t*,
                           a0_alephzero_t,
                           const char* name,
                           a0_packet_callback_t onrequest,
                           a0_packet_callback_t oncancel);
errno_t a0_rpc_client_init(a0_rpc_client_t*, a0_alephzero_t, const char* name);

#ifdef __cplusplus
}
#endif

#endif  // A0_ALEPHZERO_H
