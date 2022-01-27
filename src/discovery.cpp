#include <a0/discovery.h>
#include <a0/discovery.hpp>

#include <functional>
#include <string>

#include "c_wrap.hpp"

namespace a0 {

namespace {

struct DiscoveryImpl {
  std::function<void(const std::string&)> on_discovery;
};

}  // namespace

Discovery::Discovery(const std::string& path_pattern, std::function<void(const std::string&)> on_discovery) {
  set_c_impl<DiscoveryImpl>(
      &c,
      [&](a0_discovery_t* c, DiscoveryImpl* impl) {
        impl->on_discovery = on_discovery;

        a0_discovery_callback_t c_cb = {
            .user_data = impl,
            .fn = [](void* user_data, const char* path) {
              auto* impl = (DiscoveryImpl*)user_data;
              impl->on_discovery(path);
            }};

        return a0_discovery_init(c, path_pattern.c_str(), c_cb);
      },
      [](a0_discovery_t* c, DiscoveryImpl*) {
        a0_discovery_close(c);
      });
}

}  // namespace a0
