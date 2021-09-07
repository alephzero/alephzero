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

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

template <typename T>
class CfgVar;

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

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

struct Config : details::CppWrap<a0_config_t> {
  Config() = default;
  Config(ConfigTopic);

  Packet read(int flags = 0) const;

  void write(Packet);

  void write(string_view sv) {
    write(Packet(sv, ref));
  }

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  void mergepatch(nlohmann::json);

 private:
  void register_var(std::weak_ptr<std::function<void(const nlohmann::json&)>> updater);

  template <typename T>
  friend class CfgVar;

 public:
  template <typename T>
  CfgVar<T> var(std::string jptr_str) {
    return CfgVar<T>(*this, std::move(jptr_str));
  }

  template <typename T>
  CfgVar<T> var() {
    return var<T>("");
  }

  void update_var();

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

template <typename T>
class CfgVar {
  Config config;
  nlohmann::json::json_pointer jptr;
  std::shared_ptr<std::function<void(const nlohmann::json&)>> updater;
  mutable T cache;

  void create_updater() {
    updater = std::make_shared<std::function<void(const nlohmann::json&)>>(
        [this](const nlohmann::json& full_cfg) {
          full_cfg[jptr].get_to(cache);
        });
    config.register_var(updater);
  }

 public:
  CfgVar() = default;
  CfgVar(const CfgVar& other) {
    *this = other;
  }
  CfgVar(CfgVar&& other) {
    *this = std::move(other);
  }
  CfgVar(Config config, std::string jptr_str)
      : config{config}, jptr{jptr_str} {
    create_updater();
    (*updater)(nlohmann::json::parse(config.read().payload()));
  }

  CfgVar& operator=(const CfgVar& other) {
    config = other.config;
    jptr = other.jptr;
    cache = other.cache;
    create_updater();
    return *this;
  }
  CfgVar& operator=(CfgVar&& other) {
    config = other.config;
    jptr = std::move(other.jptr);
    cache = std::move(other.cache);
    create_updater();
    return *this;
  }

  const T& operator*() const {
    return cache;
  }

  const T* operator->() const {
    return &**this;
  }
};

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

struct ConfigListener : details::CppWrap<a0_onconfig_t> {
  ConfigListener() = default;
  ConfigListener(ConfigTopic, std::function<void(Packet)>);

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  ConfigListener(ConfigTopic, std::function<void(const nlohmann::json&)>);
  ConfigListener(ConfigTopic, std::string jptr, std::function<void(const nlohmann::json&)>);

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::function<void(Packet)> onpacket) {
  return ConfigListener(std::move(topic), std::move(onpacket));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::function<void(const nlohmann::json&)> onjson) {
  return ConfigListener(std::move(topic), std::move(onjson));
}

A0_STATIC_INLINE
ConfigListener onconfig(ConfigTopic topic, std::string jptr, std::function<void(const nlohmann::json&)> onjson) {
  return ConfigListener(std::move(topic), std::move(jptr), std::move(onjson));
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

}  // namespace a0
