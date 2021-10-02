#pragma once

#include <a0/c_wrap.hpp>
#include <a0/discovery.h>

#include <functional>

namespace a0 {

struct PathGlob : details::CppWrap<a0_pathglob_t> {
  PathGlob() = default;
  PathGlob(std::string path_pattern);

  bool match(const std::string& path) const;
};

struct Discovery : details::CppWrap<a0_discovery_t> {
  Discovery() = default;
  Discovery(
      const std::string& path_pattern,
      std::function<void(const std::string&)> on_discovery);
};

}  // namespace a0
