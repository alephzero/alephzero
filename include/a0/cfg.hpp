#pragma once

#include <a0/c_wrap.hpp>
#include <a0/cfg.h>
#include <a0/file.hpp>
#include <a0/inline.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/tid.h>
#include <a0/time.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef A0_EXT_NLOHMANN

#include <nlohmann/json.hpp>

#endif  // A0_EXT_NLOHMANN

namespace a0 {

#ifdef A0_EXT_NLOHMANN

template <typename T>
class CfgVar;

#endif  // A0_EXT_NLOHMANN

struct CfgTopic {
  std::string name;
  File::Options file_opts{File::Options::DEFAULT};

  CfgTopic() = default;

  CfgTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : CfgTopic(std::string(name)) {}

  CfgTopic(  // NOLINT(google-explicit-constructor)
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{file_opts} {}
};

struct Cfg : details::CppWrap<a0_cfg_t> {
  Cfg() = default;
  explicit Cfg(CfgTopic);

  Packet read() const;
  Packet read_blocking() const;
  Packet read_blocking(TimeMono) const;

  void write(Packet);

  void write(const char cstr[]) {
    write(Packet(cstr, ref));
  }

#ifdef A0_EXT_NLOHMANN

  void write(nlohmann::json j) {
    write(Packet(j.dump()));
  }
  void mergepatch(nlohmann::json);

 private:
  void register_var(std::weak_ptr<std::function<void(const nlohmann::json&)>> updater);

 public:
  template <typename T>
  class Var {
    std::shared_ptr<Cfg> parent;
    nlohmann::json::json_pointer jptr;
    std::shared_ptr<std::function<void(const nlohmann::json&)>> updater;
    mutable T cache;

    void create_updater() {
      updater = std::make_shared<std::function<void(const nlohmann::json&)>>(
          [this](const nlohmann::json& full_cfg) {
            full_cfg[jptr].get_to(cache);
          });
      parent->register_var(updater);
    }

   public:
    Var() = default;
    Var(const Var& other) {
      *this = other;
    }
    Var(Var&& other) {
      *this = std::move(other);
    }
    Var(Cfg parent_, std::string jptr_str)
        : parent{std::make_shared<Cfg>(parent_)}, jptr{jptr_str} {
      create_updater();
      Packet pkt = parent->read();
      const char* begin = pkt.payload().data();
      const char* end = begin + pkt.payload().size();
      (*updater)(nlohmann::json::parse(begin, end));
    }

    Var& operator=(const Var& other) {
      parent = other.parent;
      jptr = other.jptr;
      cache = other.cache;
      create_updater();
      return *this;
    }
    Var& operator=(Var&& other) {
      parent = other.parent;
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

  template <typename T>
  Var<T> var(std::string jptr_str) {
    return Var<T>(*this, std::move(jptr_str));
  }

  template <typename T>
  Var<T> var() {
    return var<T>("");
  }

  void update_var();

#endif  // A0_EXT_NLOHMANN
};

struct CfgWatcher : details::CppWrap<a0_cfg_watcher_t> {
  CfgWatcher() = default;
  CfgWatcher(CfgTopic, std::function<void(Packet)>);

#ifdef A0_EXT_NLOHMANN

  CfgWatcher(CfgTopic, std::function<void(const nlohmann::json&)>);

#endif  // A0_EXT_NLOHMANN
};

}  // namespace a0
