#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/cfg.h>
#include <a0/cfg.hpp>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/time.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "c_opts.hpp"
#include "c_wrap.hpp"

#ifdef A0_EXT_NLOHMANN

#include <a0/string_view.hpp>

#include <nlohmann/json.hpp>

#include <initializer_list>

#endif  // A0_EXT_NLOHMANN

namespace a0 {

namespace {

struct CfgImpl {
#ifdef A0_EXT_NLOHMANN
  std::vector<std::weak_ptr<std::function<void(const nlohmann::json&)>>> var_updaters;
#endif  // A0_EXT_NLOHMANN
};

}  // namespace

Cfg::Cfg(CfgTopic topic) {
  set_c_impl<CfgImpl>(
      &c,
      [&](a0_cfg_t* c, CfgImpl*) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_cfg_topic_t c_topic{topic.name.c_str(), &cfo};
        return a0_cfg_init(c, c_topic);
      },
      [](a0_cfg_t* c, CfgImpl*) {
        a0_cfg_close(c);
      });
}

A0_STATIC_INLINE
Packet Cfg_read(std::function<a0_err_t(a0_alloc_t, a0_packet_t*)> fn) {
  auto data = std::make_shared<std::vector<uint8_t>>();

  a0_alloc_t alloc = {
      .user_data = data.get(),
      .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
        auto* data = (std::vector<uint8_t>*)user_data;
        data->resize(size);
        *out = {data->data(), size};
        return A0_OK;
      },
      .dealloc = nullptr,
  };

  a0_packet_t pkt;
  check(fn(alloc, &pkt));
  return Packet(pkt, [data](a0_packet_t*) {});
}

Packet Cfg::read() const {
  return Cfg_read([&](a0_alloc_t alloc, a0_packet_t* pkt) {
    return a0_cfg_read(&*c, alloc, pkt);
  });
}

Packet Cfg::read_blocking() const {
  return Cfg_read([&](a0_alloc_t alloc, a0_packet_t* pkt) {
    return a0_cfg_read_blocking(&*c, alloc, pkt);
  });
}

Packet Cfg::read_blocking(TimeMono timeout) const {
  return Cfg_read([&](a0_alloc_t alloc, a0_packet_t* pkt) {
    return a0_cfg_read_blocking_timeout(&*c, alloc, timeout.c.get(), pkt);
  });
}

void Cfg::write(Packet pkt) {
  check(a0_cfg_write(&*c, *pkt.c));
}

bool Cfg::write_if_empty(Packet pkt) {
  bool written;
  check(a0_cfg_write_if_empty(&*c, *pkt.c, &written));
  return written;
}

void Cfg::mergepatch(Packet pkt) {
  check(a0_cfg_mergepatch(&*c, *pkt.c));
}

#ifdef A0_EXT_NLOHMANN

void Cfg::register_var(std::weak_ptr<std::function<void(const nlohmann::json&)>> updater) {
  c_impl<CfgImpl>(&c)->var_updaters.push_back(updater);
}

void Cfg::update_var() {
  auto json_cfg = nlohmann::json::parse(read().payload());

  auto* weak_updaters = &c_impl<CfgImpl>(&c)->var_updaters;
  for (size_t i = 0; i < weak_updaters->size();) {
    auto strong_updater = weak_updaters->at(i).lock();
    if (!strong_updater) {
      std::swap(weak_updaters->at(i), weak_updaters->back());
      weak_updaters->pop_back();
      continue;
    }
    (*strong_updater)(json_cfg);
    i++;
  }
}

#endif  // A0_EXT_NLOHMANN

namespace {

struct CfgWatcherImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;

#ifdef A0_EXT_NLOHMANN

  std::function<void(nlohmann::json)> onjson;

#endif  // A0_EXT_NLOHMANN
};

}  // namespace

CfgWatcher::CfgWatcher(
    CfgTopic topic,
    std::function<void(Packet)> onpacket) {
  set_c_impl<CfgWatcherImpl>(
      &c,
      [&](a0_cfg_watcher_t* c, CfgWatcherImpl* impl) {
        impl->onpacket = std::move(onpacket);

        auto cfo = c_fileopts(topic.file_opts);
        a0_cfg_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (CfgWatcherImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_onpacket = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (CfgWatcherImpl*)user_data;
              auto data = std::make_shared<std::vector<uint8_t>>();
              std::swap(*data, impl->data);
              impl->onpacket(Packet(pkt, [data](a0_packet_t*) {}));
            }};

        return a0_cfg_watcher_init(c, c_topic, alloc, c_onpacket);
      },
      [](a0_cfg_watcher_t* c, CfgWatcherImpl*) {
        a0_cfg_watcher_close(c);
      });
}

#ifdef A0_EXT_NLOHMANN

CfgWatcher::CfgWatcher(
    CfgTopic topic,
    std::function<void(const nlohmann::json&)> onjson)
    : CfgWatcher(topic, [onjson](Packet pkt) {
        onjson(nlohmann::json::parse(pkt.payload()));
      }) {}

#endif  // A0_EXT_NLOHMANN

}  // namespace a0
