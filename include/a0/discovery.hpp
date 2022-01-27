#pragma once

#include <a0/c_wrap.hpp>
#include <a0/discovery.h>

#include <functional>

namespace a0 {

struct Discovery : details::CppWrap<a0_discovery_t> {
  Discovery() = default;
  Discovery(
      const std::string& path_pattern,
      std::function<void(const std::string&)> on_discovery);
};

}  // namespace a0
