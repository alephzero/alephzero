#include <a0/common.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <errno.h>
#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <utility>

#include "strutil.hpp"

using json = nlohmann::json;

namespace {

struct topic_map_val {
  std::string container;
  std::string topic;
};

void from_json(const json& j, topic_map_val& val) {
  j.at("container").get_to(val.container);
  j.at("topic").get_to(val.topic);
}

struct topic_manager_options {
  std::string container;

  std::unordered_map<std::string, topic_map_val> subscriber_maps;
  std::unordered_map<std::string, topic_map_val> rpc_client_maps;
  std::unordered_map<std::string, topic_map_val> prpc_client_maps;
};

void from_json(const json& j, topic_manager_options& opts) {
  j.at("container").get_to(opts.container);
  if (j.count("subscriber_maps")) {
    j.at("subscriber_maps").get_to(opts.subscriber_maps);
  }
  if (j.count("rpc_client_maps")) {
    j.at("rpc_client_maps").get_to(opts.rpc_client_maps);
  }
  if (j.count("prpc_client_maps")) {
    j.at("prpc_client_maps").get_to(opts.prpc_client_maps);
  }
}

}  // namespace

struct a0_topic_manager_impl_s {
  topic_manager_options opts;

  // TODO: Make this configurable.
  a0_shm_options_t default_shm_options;

  a0_topic_manager_impl_s() {
    default_shm_options.size = 16 * 1024 * 1024;
  }
};

errno_t a0_topic_manager_init(a0_topic_manager_t* tm, const char* jsonstr) {
  using json = nlohmann::json;

  topic_manager_options opts;
  try {
    json::parse(jsonstr).get_to(opts);
  } catch (...) {
    return EINVAL;
  }

  tm->_impl = new a0_topic_manager_impl_t;
  tm->_impl->opts = std::move(opts);

  return A0_OK;
}

errno_t a0_topic_manager_close(a0_topic_manager_t* tm) {
  if (!tm || !tm->_impl) {
    return ESHUTDOWN;
  }

  delete tm->_impl;
  tm->_impl = nullptr;

  return A0_OK;
}

errno_t a0_topic_manager_container_name(const a0_topic_manager_t* tm, const char** out) {
  *out = tm->_impl->opts.container.c_str();
  return A0_OK;
}

constexpr char kConfigTopicTemplate[] = "/a0_config__%s";
constexpr char kLogTopicTemplate[] = "/a0_log_%s__%s";
constexpr char kPubsubTopicTemplate[] = "/a0_pubsub__%s__%s";
constexpr char kRpcTopicTemplate[] = "/a0_rpc__%s__%s";
constexpr char kPrpcTopicTemplate[] = "/a0_prpc__%s__%s";

errno_t a0_topic_manager_open_config_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kConfigTopicTemplate, tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_log_crit_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "crit", tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}
errno_t a0_topic_manager_open_log_err_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "err", tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}
errno_t a0_topic_manager_open_log_warn_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "warn", tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}
errno_t a0_topic_manager_open_log_info_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "info", tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}
errno_t a0_topic_manager_open_log_dbg_topic(const a0_topic_manager_t* tm, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kLogTopicTemplate, "dbg", tm->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_publisher_topic(const a0_topic_manager_t* tm,
                                              const char* name,
                                              a0_shm_t* out) {
  auto path = a0::strutil::fmt(kPubsubTopicTemplate, tm->_impl->opts.container.c_str(), name);
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_subscriber_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  if (!tm->_impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &tm->_impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_rpc_server_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  auto path = a0::strutil::fmt(kRpcTopicTemplate, tm->_impl->opts.container.c_str(), name);
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_rpc_client_topic(const a0_topic_manager_t* tm,
                                               const char* name,
                                               a0_shm_t* out) {
  if (!tm->_impl->opts.rpc_client_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &tm->_impl->opts.rpc_client_maps.at(name);
  auto path =
      a0::strutil::fmt(kRpcTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_prpc_server_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_shm_t* out) {
  auto path = a0::strutil::fmt(kPrpcTopicTemplate, tm->_impl->opts.container.c_str(), name);
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_prpc_client_topic(const a0_topic_manager_t* tm,
                                                const char* name,
                                                a0_shm_t* out) {
  if (!tm->_impl->opts.prpc_client_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &tm->_impl->opts.prpc_client_maps.at(name);
  auto path =
      a0::strutil::fmt(kPrpcTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  return a0_shm_open(path.c_str(), &tm->_impl->default_shm_options, out);
}
