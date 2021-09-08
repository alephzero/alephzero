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
  File::Options file_opts{File::Options::DEFAULT};

  ConfigTopic() = default;

  ConfigTopic(const char* name)
      : ConfigTopic(std::string(name)) {}

  ConfigTopic(
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{std::move(file_opts)} {}
};

struct Config : details::CppWrap<a0_config_t> {
  Config();
  Config(ConfigTopic);

  Packet read(int flags = 0) const;

  void write(Packet);

  void write(const char cstr[]) {
    write(Packet(cstr, ref));
  }

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  void write(nlohmann::json j) {
    write(Packet(j.dump()));
  }
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
  std::shared_ptr<Config> config;
  nlohmann::json::json_pointer jptr;
  std::shared_ptr<std::function<void(const nlohmann::json&)>> updater;
  mutable T cache;

  void create_updater() {
    updater = std::make_shared<std::function<void(const nlohmann::json&)>>(
        [this](const nlohmann::json& full_cfg) {
          full_cfg[jptr].get_to(cache);
        });
    config->register_var(updater);
  }

 public:
  CfgVar() = default;
  CfgVar(const CfgVar& other) {
    *this = other;
  }
  CfgVar(CfgVar&& other) {
    *this = std::move(other);
  }
  CfgVar(Config config_, std::string jptr_str)
      : config{std::make_shared<Config>(config_)}, jptr{jptr_str} {
    create_updater();
    (*updater)(nlohmann::json::parse(config->read().payload()));
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
  ConfigListener(std::function<void(Packet)>);
  ConfigListener(ConfigTopic, std::function<void(Packet)>);

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  ConfigListener(std::function<void(const nlohmann::json&)>);
  ConfigListener(ConfigTopic, std::function<void(const nlohmann::json&)>);

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

}  // namespace a0
