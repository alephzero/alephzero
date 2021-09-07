#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/config.h>
#include <a0/config.hpp>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/tid.h>
#include <a0/transport.hpp>
#include <a0/writer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "c_wrap.hpp"
#include "config_common.h"
#include "file_opts.hpp"

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

#include <nlohmann/json.hpp>

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

namespace a0 {

namespace {

struct ConfigListenerImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  std::function<void(nlohmann::json)> onjson;
  nlohmann::json::json_pointer jptr;

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

}  // namespace

ConfigListener::ConfigListener(
    ConfigTopic topic,
    std::function<void(Packet)> onpacket) {
  set_c_impl<ConfigListenerImpl>(
      &c,
      [&](a0_onconfig_t* c, ConfigListenerImpl* impl) {
        impl->onpacket = std::move(onpacket);

        auto cfo = c_fileopts(topic.file_opts);
        a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (ConfigListenerImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_onpacket = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (ConfigListenerImpl*)user_data;
              auto data = std::make_shared<std::vector<uint8_t>>();
              std::swap(*data, impl->data);
              impl->onpacket(Packet(pkt, [data](a0_packet_t*) {}));
            }};

        return a0_onconfig_init(c, c_topic, alloc, c_onpacket);
      },
      [](a0_onconfig_t* c, ConfigListenerImpl*) {
        a0_onconfig_close(c);
      });
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

ConfigListener::ConfigListener(
    ConfigTopic topic,
    std::function<void(nlohmann::json)> onjson)
    : ConfigListener(std::move(topic), "/", std::move(onjson)) {}

ConfigListener::ConfigListener(
    ConfigTopic topic,
    std::string jptr_str,
    std::function<void(nlohmann::json)> onjson) {
  set_c_impl<ConfigListenerImpl>(
      &c,
      [&](a0_onconfig_t* c, ConfigListenerImpl* impl) {
        impl->onjson = std::move(onjson);
        impl->jptr = nlohmann::json::json_pointer(jptr_str);

        auto cfo = c_fileopts(topic.file_opts);
        a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (ConfigListenerImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_onpacket = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (ConfigListenerImpl*)user_data;
              auto json = nlohmann::json::parse(
                  string_view((const char*)pkt.payload.ptr, pkt.payload.size));
              impl->onjson(json[impl->jptr]);
            }};

        return a0_onconfig_init(c, c_topic, alloc, c_onpacket);
      },
      [](a0_onconfig_t* c, ConfigListenerImpl*) {
        a0_onconfig_close(c);
      });
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

Packet config_read(ConfigTopic topic, int flags) {
  auto cfo = c_fileopts(topic.file_opts);
  a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

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
  check(a0_config_read(c_topic, alloc, flags, &pkt));

  return Packet(pkt, [data](a0_packet_t*) {});
}

void config_write(ConfigTopic topic, Packet pkt) {
  auto cfo = c_fileopts(topic.file_opts);
  a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

  check(a0_config_write(c_topic, *pkt.c));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

void config_mergepatch(ConfigTopic topic, nlohmann::json update) {
  auto cfo = c_fileopts(topic.file_opts);
  a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

  a0_file_t file;
  check(a0_config_topic_open(c_topic, &file));

  Writer w(cpp_wrap<File>(file));

  a0_middleware_t mergepatch_middleware = {
      .user_data = &update,
      .close = NULL,
      .process = NULL,
      .process_locked = [](
          void* user_data,
          a0_transport_locked_t tlk,
          a0_packet_t* pkt,
          a0_middleware_chain_t chain) mutable {
        auto* update = (nlohmann::json*)user_data;
        auto cpp_tlk = cpp_wrap<TransportLocked>(tlk);

        std::string serial;

        if (cpp_tlk.empty()) {
          serial = update->dump();
        } else {
          cpp_tlk.jump_tail();
          auto frame = cpp_tlk.frame();
          auto flat_packet = cpp_wrap<FlatPacket>({frame.data, frame.hdr.data_size});
          auto doc = nlohmann::json::parse(flat_packet.payload());
          doc.merge_patch(*update);
          serial = doc.dump();
        }

        pkt->payload = (a0_buf_t){(uint8_t*)serial.data(), serial.size()};
        return a0_middleware_chain(chain, pkt);
      },
  };

  w.push(cpp_wrap<Middleware>(mergepatch_middleware));
  w.push(add_standard_headers());

  w.write("");
}

namespace details {

struct ConfigRegistrar {
  std::mutex mu;
  std::vector<std::weak_ptr<cfg_cache>> caches;

  static ConfigRegistrar* get() {
    static ConfigRegistrar singleton;
    return &singleton;
  }
};

void register_cfg(std::weak_ptr<cfg_cache> cfg) {
  auto* reg = ConfigRegistrar::get();
  std::unique_lock<std::mutex> lk{reg->mu};
  reg->caches.push_back(cfg);
}

}  // namespace details

void update_configs() {
  auto* reg = details::ConfigRegistrar::get();
  std::unique_lock<std::mutex> lk{reg->mu};

  for (size_t i = 0; i < reg->caches.size();) {
    auto locked_cache = reg->caches[i].lock();
    if (!locked_cache) {
      std::swap(reg->caches[i], reg->caches.back());
      reg->caches.pop_back();
      continue;
    }

    std::unique_lock<std::mutex> cfg_lk{locked_cache->mu};
    locked_cache->valid[a0_tid()] = false;
    i++;
  }
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

}  // namespace a0
