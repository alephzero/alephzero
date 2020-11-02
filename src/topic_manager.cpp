#include <a0/arena.h>
#include <a0/errno.h>
#include <a0/topic_manager.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

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

static constexpr std::string_view TOPIC_TMPL_CONFIG = "a0_config__%s";
static constexpr std::string_view TOPIC_TMPL_HEARTBEAT = "a0_heartbeat__%s";
static constexpr std::string_view TOPIC_TMPL_LOG = "a0_log_%s__%s";
static constexpr std::string_view TOPIC_TMPL_PUBSUB = "a0_pubsub__%s__%s";
static constexpr std::string_view TOPIC_TMPL_RPC = "a0_rpc__%s__%s";
static constexpr std::string_view TOPIC_TMPL_PRPC = "a0_prpc__%s__%s";

errno_t a0_topic_manager_open_config_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_CONFIG, tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_heartbeat_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_HEARTBEAT, tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_log_crit_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_LOG, "crit", tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}
errno_t a0_topic_manager_open_log_err_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_LOG, "err", tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}
errno_t a0_topic_manager_open_log_warn_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_LOG, "warn", tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}
errno_t a0_topic_manager_open_log_info_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_LOG, "info", tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}
errno_t a0_topic_manager_open_log_dbg_topic(const a0_topic_manager_t* tm, a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_LOG, "dbg", tm->container);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_publisher_topic(const a0_topic_manager_t* tm,
                                              const char* name,
                                              a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_PUBSUB, tm->container, name);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_subscriber_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_file_t* out) {
  const a0_topic_alias_t* alias;
  A0_RETURN_ERR_ON_ERR(
      find_alias(tm->subscriber_aliases, tm->subscriber_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(TOPIC_TMPL_PUBSUB, alias->target_container, alias->target_topic);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_rpc_server_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_RPC, tm->container, name);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_rpc_client_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_file_t* out) {
  const a0_topic_alias_t* alias;
  A0_RETURN_ERR_ON_ERR(
      find_alias(tm->rpc_client_aliases, tm->rpc_client_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(TOPIC_TMPL_RPC, alias->target_container, alias->target_topic);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_prpc_server_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_file_t* out) {
  auto path = a0::strutil::fmt(TOPIC_TMPL_PRPC, tm->container, name);
  return a0_file_open(path.c_str(), nullptr, out);
}

errno_t a0_topic_manager_open_prpc_client_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_file_t* out) {
  const a0_topic_alias_t* alias;
  A0_RETURN_ERR_ON_ERR(
      find_alias(tm->prpc_client_aliases, tm->prpc_client_aliases_size, name, &alias));
  auto path = a0::strutil::fmt(TOPIC_TMPL_PRPC, alias->target_container, alias->target_topic);
  return a0_file_open(path.c_str(), nullptr, out);
}
