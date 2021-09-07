#pragma once

#include <a0/c_wrap.hpp>
#include <a0/config.h>
#include <a0/file.hpp>
#include <a0/inline.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/tid.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

#include <nlohmann/json.hpp>

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

namespace a0 {

struct ConfigTopic {
  std::string name;
  File::Options file_opts;

  ConfigTopic(
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{std::move(file_opts)} {}

  ConfigTopic(const char* name)
      : ConfigTopic(std::string(name)) {}
};

struct ConfigListener : details::CppWrap<a0_onconfig_t> {
  ConfigListener() = default;
  ConfigListener(ConfigTopic, std::function<void(Packet)>);

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  ConfigListener(ConfigTopic, std::function<void(nlohmann::json)>);
  ConfigListener(ConfigTopic, std::string jptr, std::function<void(nlohmann::json)>);

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::function<void(Packet)> onpacket) {
  return ConfigListener(std::move(topic), std::move(onpacket));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::function<void(nlohmann::json)> onjson) {
  return ConfigListener(std::move(topic), std::move(onjson));
}

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::string jptr, std::function<void(nlohmann::json)> onjson) {
  return ConfigListener(std::move(topic), std::move(jptr), std::move(onjson));
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

Packet read_config(ConfigTopic, int flags = 0);

void write_config(ConfigTopic, Packet);

A0_STATIC_INLINE
void write_config(ConfigTopic topic, string_view sv) {
  write_config(topic, Packet(sv, ref));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

void mergepatch_config(ConfigTopic, nlohmann::json);

namespace details {

struct cfg_cache {
  std::mutex mu;
  std::unordered_map<uint32_t /* tid */, bool> valid;
};

void register_cfg(std::weak_ptr<cfg_cache>);

}  // namespace details

// Variable wrapper that gets values from alephzero configuration.
//
// cfg uses json pointers (https://tools.ietf.org/html/rfc6901) to
// specify which part of the full configuration to bind.
//
// The value in cfg does not change automatically when updated
// externally. update_configs() must be called to update the value.
//
// Each thread has its own cached value, to prevent distruption in
// another thread. update_configs() MUST be called in each thread.
//
// Example:
//   // Configuration set to { "foo": { "bar": 7, "baz": 3 } }
//   cfg<int> x("mynode", "/foo/bar");
//   *x == 7;
//   // Configuration externally changed to { "foo": { "bar": 1, "baz": 3 } }
//   *x == 7;
//   update_configs();
//   *x == 1;
//
template <typename T>
class cfg {
  const ConfigTopic topic;
  const nlohmann::json::json_pointer jptr;
  std::shared_ptr<details::cfg_cache> cache;
  mutable std::unordered_map<uint32_t /* tid */, T> value;

 public:
  cfg(ConfigTopic topic, std::string jptr)
      : topic{std::move(topic)},
        jptr(nlohmann::json::json_pointer(jptr)),
        cache{std::make_shared<details::cfg_cache>()} {
    details::register_cfg(cache);
  }

  const T& operator*() const {
    std::unique_lock<std::mutex> lk{cache->mu};
    if (!cache->valid[a0_tid()]) {
      auto json_cfg = nlohmann::json::parse(read_config(topic).payload());
      json_cfg[jptr].get_to(value[a0_tid()]);
      cache->valid[a0_tid()] = true;
    }

    return value[a0_tid()];
  }

  const T* operator->() const {
    return &**this;
  }
};

// Updates the value of all cfg IN THE CURRENT THREAD.
// This is to prevent one thread (say a subscriber callback)
// from breaking the logic of another thread.
//
// Common widget pattern:
//   cfg<...> x("mynode", "/foo");
//   while (true) {
//     update_configs();
//     ...
//     *x ...
//     ...
//   }
//
//   or
//
//   cfg<...> x("mynode", "/foo");
//   Subscriber s0(..., []() { update_configs(); ...; *x; ... });
//   Subscriber s1(..., []() { update_configs(); ...; *x; ... });
//   ...
//   pause();
//
void update_configs();

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

}  // namespace a0
