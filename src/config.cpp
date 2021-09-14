#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/config.h>
#include <a0/config.hpp>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "c_wrap.hpp"
#include "file_opts.hpp"

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

#include <a0/middleware.h>
#include <a0/middleware.hpp>
#include <a0/string_view.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>
#include <a0/writer.hpp>

#include <initializer_list>
#include <type_traits>

#include <nlohmann/json.hpp>

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

namespace a0 {

namespace {

struct ConfigImpl {
#ifdef A0_CXX_CONFIG_USE_NLOHMANN
  std::vector<std::weak_ptr<std::function<void(const nlohmann::json&)>>> var_updaters;
#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

}  // namespace

Config::Config()
    : Config(ConfigTopic{}) {}

Config::Config(ConfigTopic topic) {
  set_c_impl<ConfigImpl>(
      &c,
      [&](a0_config_t* c, ConfigImpl*) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_config_topic_t c_topic{topic.name.c_str(), &cfo};
        return a0_config_init(c, c_topic);
      },
      [](a0_config_t* c, ConfigImpl*) {
        a0_config_close(c);
      });
}

Packet Config::read(int flags) const {
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
  check(a0_config_read(&*c, alloc, flags, &pkt));

  return Packet(pkt, [data](a0_packet_t*) {});
}

void Config::write(Packet pkt) {
  check(a0_config_write(&*c, *pkt.c));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

void Config::mergepatch(nlohmann::json update) {
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

  cpp_wrap<Writer>(&c->_writer)
      .wrap(cpp_wrap<Middleware>(mergepatch_middleware))
      .write("");
}

void Config::register_var(std::weak_ptr<std::function<void(const nlohmann::json&)>> updater) {
  c_impl<ConfigImpl>(&c)->var_updaters.push_back(updater);
}

void Config::update_var() {
  auto json_cfg = nlohmann::json::parse(read().payload());

  auto* weak_updaters = &c_impl<ConfigImpl>(&c)->var_updaters;
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

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

namespace {

struct ConfigListenerImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

  std::function<void(nlohmann::json)> onjson;

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
};

}  // namespace

ConfigListener::ConfigListener(std::function<void(Packet)> onpacket)
    : ConfigListener(ConfigTopic{}, std::move(onpacket)) {}

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

ConfigListener::ConfigListener(std::function<void(const nlohmann::json&)> onjson)
    : ConfigListener(ConfigTopic{}, std::move(onjson)) {}

ConfigListener::ConfigListener(
    ConfigTopic topic,
    std::function<void(const nlohmann::json&)> onjson) {
  set_c_impl<ConfigListenerImpl>(
      &c,
      [&](a0_onconfig_t* c, ConfigListenerImpl* impl) {
        impl->onjson = std::move(onjson);

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
                  string_view((const char*)pkt.payload.data, pkt.payload.size));
              impl->onjson(json);
            }};

        return a0_onconfig_init(c, c_topic, alloc, c_onpacket);
      },
      [](a0_onconfig_t* c, ConfigListenerImpl*) {
        a0_onconfig_close(c);
      });
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

}  // namespace a0
