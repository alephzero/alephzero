#include <a0/discovery.h>
#include <a0/discovery.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "c_wrap.hpp"

namespace a0 {

PathGlob::PathGlob(std::string path_pattern) {
  auto path_pattern_mem = std::make_shared<std::string>(std::move(path_pattern));
  set_c<a0_pathglob_t>(
      &c,
      [&](a0_pathglob_t* c) {
        return a0_pathglob_init(c, path_pattern_mem->c_str());
      },
      [path_pattern_mem](a0_pathglob_t*) {});
}

bool PathGlob::match(const std::string& path) const {
  CHECK_C;
  bool result;
  check(a0_pathglob_match(&*c, path.c_str(), &result));
  return result;
}

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
