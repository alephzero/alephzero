#ifndef A0_TOPIC_MANAGER_H
#define A0_TOPIC_MANAGER_H

#include <a0/arena.h>
#include <a0/common.h>

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO(lshamis): Replace with a node class, to directly initializes protocols.
//                With a topic-manager, rpc servers must use a single transport.

typedef struct a0_topic_alias_s {
  const char* name;
  const char* target_container;
  const char* target_topic;
} a0_topic_alias_t;

typedef struct a0_topic_manager_s {
  const char* container;

  a0_topic_alias_t* subscriber_aliases;
  size_t subscriber_aliases_size;

  a0_topic_alias_t* rpc_client_aliases;
  size_t rpc_client_aliases_size;

  a0_topic_alias_t* prpc_client_aliases;
  size_t prpc_client_aliases_size;
} a0_topic_manager_t;

errno_t a0_topic_manager_open_config_topic(const a0_topic_manager_t*, a0_file_t* out);

errno_t a0_topic_manager_open_heartbeat_topic(const a0_topic_manager_t*, a0_file_t* out);

errno_t a0_topic_manager_open_log_crit_topic(const a0_topic_manager_t*, a0_file_t* out);
errno_t a0_topic_manager_open_log_err_topic(const a0_topic_manager_t*, a0_file_t* out);
errno_t a0_topic_manager_open_log_warn_topic(const a0_topic_manager_t*, a0_file_t* out);
errno_t a0_topic_manager_open_log_info_topic(const a0_topic_manager_t*, a0_file_t* out);
errno_t a0_topic_manager_open_log_dbg_topic(const a0_topic_manager_t*, a0_file_t* out);

errno_t a0_topic_manager_open_publisher_topic(const a0_topic_manager_t*,
                                              const char*,
                                              a0_file_t* out);
errno_t a0_topic_manager_open_subscriber_topic(const a0_topic_manager_t*,
                                               const char*,
                                               a0_file_t* out);
errno_t a0_topic_manager_open_rpc_server_topic(const a0_topic_manager_t*,
                                               const char*,
                                               a0_file_t* out);
errno_t a0_topic_manager_open_rpc_client_topic(const a0_topic_manager_t*,
                                               const char*,
                                               a0_file_t* out);
errno_t a0_topic_manager_open_prpc_server_topic(const a0_topic_manager_t*,
                                                const char*,
                                                a0_file_t* out);
errno_t a0_topic_manager_open_prpc_client_topic(const a0_topic_manager_t*,
                                                const char*,
                                                a0_file_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_TOPIC_MANAGER_H
