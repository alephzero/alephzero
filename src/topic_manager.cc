#include <a0/common.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <errno.h>
#include <string.h>

#include <string>

#include "macros.h"
#include "strutil.hpp"

A0_STATIC_INLINE
errno_t find_alias(a0_topic_alias_t* aliases,
                   size_t aliases_size,
                   const char* name,
                   const a0_topic_alias_t** alias) {
  for (size_t i = 0; i < aliases_size; i++) {
    if (!strcmp(name, aliases[i].name)) {
      *alias = &aliases[i];
      return A0_OK;
    }
  }
  return EINVAL;
}

// TODO: Make this configurable.
A0_STATIC_INLINE
const a0_shm_options_t* default_shm_options() {
  static a0_shm_options_t shmopt{.size = 16 * 1024 * 1024};
  return &shmopt;
}

constexpr char kConfigTopicTemplate[] = "/a0_config__%s";
constexpr char kLogTopicTemplate[] = "/a0_log_%s__%s";
constexpr char kPubsubTopicTemplate[] = "/a0_pubsub__%s__%s";
constexpr char kRpcTopicTemplate[] = "/a0_rpc__%s__%s";
constexpr char kPrpcTopicTemplate[] = "/a0_prpc__%s__%s";

errno_t a0_topic_manager_open_config_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kConfigTopicTemplate, tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_log_crit_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "crit", tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}
errno_t a0_topic_manager_open_log_err_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "err", tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}
errno_t a0_topic_manager_open_log_warn_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "warn", tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}
errno_t a0_topic_manager_open_log_info_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "info", tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}
errno_t a0_topic_manager_open_log_dbg_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "dbg", tm->container);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_publisher_topic(const a0_topic_manager_t* tm,
                                              const char* name,
                                              a0_shm_t* out) {
  auto path = a0::strutil::fmt(kPubsubTopicTemplate, tm->container, name);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_subscriber_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  const a0_topic_alias_t* alias;
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      find_alias(tm->subscriber_aliases, tm->subscriber_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(kPubsubTopicTemplate, alias->target_container, alias->target_topic);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_rpc_server_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  auto path = a0::strutil::fmt(kRpcTopicTemplate, tm->container, name);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_rpc_client_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  const a0_topic_alias_t* alias;
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      find_alias(tm->rpc_client_aliases, tm->rpc_client_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(kRpcTopicTemplate, alias->target_container, alias->target_topic);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_prpc_server_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_shm_t* out) {
  auto path = a0::strutil::fmt(kPrpcTopicTemplate, tm->container, name);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}

errno_t a0_topic_manager_open_prpc_client_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_shm_t* out) {
  const a0_topic_alias_t* alias;
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      find_alias(tm->prpc_client_aliases, tm->prpc_client_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(kPrpcTopicTemplate, alias->target_container, alias->target_topic);
  return a0_shm_open(path.c_str(), default_shm_options(), out);
}
