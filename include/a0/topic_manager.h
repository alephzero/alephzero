#ifndef A0_TOPIC_MANAGER_H
#define A0_TOPIC_MANAGER_H

#include <a0/common.h>
#include <a0/shmobj.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_topic_map_s {
  const char* name;

  const char* container;
  const char* topic;
} a0_topic_map_t;

typedef struct a0_topic_manager_options_s {
  const char* container;

  size_t num_subscriber_maps;
  a0_topic_map_t* subscriber_maps;

  size_t num_rpc_client_maps;
  a0_topic_map_t* rpc_client_maps;
} a0_topic_manager_options_t;

typedef struct a0_topic_manager_impl_s a0_topic_manager_impl_t;

typedef struct a0_topic_manager_s {
  a0_topic_manager_impl_t* _impl;
} a0_topic_manager_t;

errno_t a0_topic_manager_init(a0_topic_manager_t*, a0_topic_manager_options_t);
errno_t a0_topic_manager_init_jsonstr(a0_topic_manager_t*, const char*);
errno_t a0_topic_manager_close(a0_topic_manager_t*);

errno_t a0_topic_manager_config_topic(a0_topic_manager_t*, a0_shmobj_t* out);
errno_t a0_topic_manager_publisher_topic(a0_topic_manager_t*, const char*, a0_shmobj_t* out);
errno_t a0_topic_manager_subscriber_topic(a0_topic_manager_t*, const char*, a0_shmobj_t* out);
errno_t a0_topic_manager_rpc_server_topic(a0_topic_manager_t*, const char*, a0_shmobj_t* out);
errno_t a0_topic_manager_rpc_client_topic(a0_topic_manager_t*, const char*, a0_shmobj_t* out);

errno_t a0_topic_manager_unref(a0_topic_manager_t*, a0_shmobj_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_TOPIC_MANAGER_H
