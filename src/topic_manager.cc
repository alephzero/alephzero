#include <a0/common.h>         // for errno_t, A0_OK
#include <a0/shm.h>            // for a0_shm_open, a0_shm_t, a0_shm_options_t
#include <a0/topic_manager.h>  // for a0_topic_manager_t, a0_topic_map_t

#include <errno.h>            // for EINVAL, ESHUTDOWN
#include <nlohmann/json.hpp>  // for basic_json, json

#include <string>         // for string, basic_string
#include <unordered_map>  // for unordered_map, unordered_map<>::mapped...
#include <utility>        // for move

#include "strutil.hh"  // for strutil

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
};

void from_json(const json& j, topic_manager_options& opts) {
  j.at("container").get_to(opts.container);
  if (j.count("subscriber_maps")) {
    j.at("subscriber_maps").get_to(opts.subscriber_maps);
  }
  if (j.count("rpc_client_maps")) {
    j.at("rpc_client_maps").get_to(opts.rpc_client_maps);
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

errno_t a0_topic_manager_init(a0_topic_manager_t* topic_manager, const char* jsonstr) {
  using json = nlohmann::json;

  topic_manager_options opts;
  try {
    json::parse(jsonstr).get_to(opts);
  } catch (...) {
    return EINVAL;
  }

  topic_manager->_impl = new a0_topic_manager_impl_t;
  topic_manager->_impl->opts = std::move(opts);

  return A0_OK;
}

errno_t a0_topic_manager_close(a0_topic_manager_t* topic_manager) {
  if (!topic_manager || !topic_manager->_impl) {
    return ESHUTDOWN;
  }

  delete topic_manager->_impl;
  topic_manager->_impl = nullptr;

  return A0_OK;
}

constexpr char kConfigTopicTemplate[] = "/a0_config__%s";
constexpr char kPubsubTopicTemplate[] = "/a0_pubsub__%s__%s";
constexpr char kRpcTopicTemplate[] = "/a0_rpc__%s__%s";

errno_t a0_topic_manager_open_config_topic(a0_topic_manager_t* topic_manager, a0_shm_t* out) {
  auto path = a0::strutil::fmt(kConfigTopicTemplate, topic_manager->_impl->opts.container.c_str());
  return a0_shm_open(path.c_str(), &topic_manager->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_publisher_topic(a0_topic_manager_t* topic_manager,
                                              const char* name,
                                              a0_shm_t* out) {
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, topic_manager->_impl->opts.container.c_str(), name);
  return a0_shm_open(path.c_str(), &topic_manager->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_subscriber_topic(a0_topic_manager_t* topic_manager,
                                               const char* name,
                                               a0_shm_t* out) {
  if (!topic_manager->_impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &topic_manager->_impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  return a0_shm_open(path.c_str(), &topic_manager->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_rpc_server_topic(a0_topic_manager_t* topic_manager,
                                               const char* name,
                                               a0_shm_t* out) {
  auto path =
      a0::strutil::fmt(kRpcTopicTemplate, topic_manager->_impl->opts.container.c_str(), name);
  return a0_shm_open(path.c_str(), &topic_manager->_impl->default_shm_options, out);
}

errno_t a0_topic_manager_open_rpc_client_topic(a0_topic_manager_t* topic_manager,
                                               const char* name,
                                               a0_shm_t* out) {
  if (!topic_manager->_impl->opts.rpc_client_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &topic_manager->_impl->opts.rpc_client_maps.at(name);
  auto path =
      a0::strutil::fmt(kRpcTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  return a0_shm_open(path.c_str(), &topic_manager->_impl->default_shm_options, out);
}
