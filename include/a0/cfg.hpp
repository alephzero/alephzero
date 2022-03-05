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
  Cfg(const Cfg&) = default;
  explicit Cfg(CfgTopic);

  Packet read() const;
  Packet read_blocking() const;
  Packet read_blocking(TimeMono) const;

  void write(Packet);
  void write(const char cstr[]) {
    write(Packet(cstr, ref));
  }
  void write(std::string str) {
    write(Packet(std::move(str)));
  }

  bool write_if_empty(Packet);
  bool write_if_empty(const char cstr[]) {
    return write_if_empty(Packet(cstr, ref));
  }
  bool write_if_empty(std::string str) {
    return write_if_empty(Packet(std::move(str)));
  }

  void mergepatch(Packet);
  void mergepatch(const char cstr[]) {
    mergepatch(Packet(cstr, ref));
  }
  void mergepatch(std::string str) {
    mergepatch(Packet(std::move(str)));
  }

#ifdef A0_EXT_NLOHMANN

  void write(nlohmann::json j) {
    write(Packet(j.dump()));
  }
  bool write_if_empty(nlohmann::json j) {
    return write_if_empty(Packet(j.dump()));
  }

  void mergepatch(nlohmann::json j) {
    mergepatch(Packet(j.dump()));
  }

 private:
  void register_var(std::weak_ptr<std::function<void(const nlohmann::json&)>> updater);

 public:
  template <typename T>
  class Var {
    struct Impl {
      std::shared_ptr<Cfg> parent;
      nlohmann::json::json_pointer jptr;
      std::shared_ptr<std::function<void(const nlohmann::json&)>> updater;
      T cache;
      bool cache_populated{false};
      std::string parse_error;
    };
    std::shared_ptr<Impl> impl;

    void create_updater() {
      std::weak_ptr<Impl> weak_impl = impl;
      impl->updater = std::make_shared<std::function<void(const nlohmann::json&)>>(
          [weak_impl](const nlohmann::json& full_cfg) {
            auto strong_impl = weak_impl.lock();
            if (!strong_impl) {
              return;
            }
            strong_impl->cache_populated = false;
            strong_impl->parse_error.clear();
            try {
              full_cfg.at(strong_impl->jptr).get_to(strong_impl->cache);
              strong_impl->cache_populated = true;
            } catch (const std::exception& e) {
              strong_impl->parse_error = e.what();
            }
          });
      impl->parent->register_var(impl->updater);
    }

   public:
    Var() = default;
    Var(const Var& other) {
      *this = other;
    }
    Var(Var&& other) noexcept {
      *this = std::move(other);
    }
    Var(Cfg parent_, std::string jptr_str)
        : impl{std::make_shared<Impl>()} {
      impl->parent = std::make_shared<Cfg>(parent_);
      impl->jptr = nlohmann::json::json_pointer{jptr_str};
      create_updater();
      try {
        Packet pkt = impl->parent->read();
        const char* begin = pkt.payload().data();
        const char* end = begin + pkt.payload().size();
        (*impl->updater)(nlohmann::json::parse(begin, end));
      } catch (...) {
      }
    }

    Var& operator=(const Var& other) {
      impl = other.impl;
      return *this;
    }

    Var& operator=(Var&& other) noexcept {
      impl = std::move(other.impl);
      return *this;
    }

    const T& operator*() const {
      if (!impl->cache_populated) {
        std::string err = "Cfg::Var(jptr=" + std::string(impl->jptr) + ") ";
        err += impl->parse_error.empty() ? "has no data" : "parse error: " + impl->parse_error;
        throw std::runtime_error(err);
      }
      return impl->cache;
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
