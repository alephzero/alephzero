#include <a0/config.hpp>
#include <a0/string_view.hpp>

#include "c_wrap.hpp"
#include "file_opts.hpp"

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
            }
        };

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
                    // (const char*)pkt.payload.ptr
                    string_view((const char*)pkt.payload.ptr, pkt.payload.size)
                    );
                impl->onjson(json[impl->jptr]);
            }
        };

        return a0_onconfig_init(c, c_topic, alloc, c_onpacket);
      },
      [](a0_onconfig_t* c, ConfigListenerImpl*) {
        a0_onconfig_close(c);
      });
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

Packet config(ConfigTopic topic, int flags) {
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
  check(a0_config(c_topic, alloc, flags, &pkt));

  return Packet(pkt, [data](a0_packet_t*) {});
}

void write_config(ConfigTopic topic, Packet pkt) {
  auto cfo = c_fileopts(topic.file_opts);
  a0_config_topic_t c_topic{topic.name.c_str(), &cfo};

  check(a0_write_config(c_topic, *pkt.c));
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

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
