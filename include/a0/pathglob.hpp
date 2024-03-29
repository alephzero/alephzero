#pragma once

#include <a0/c_wrap.hpp>
#include <a0/pathglob.h>

#include <functional>

namespace a0 {

struct PathGlob : details::CppWrap<a0_pathglob_t> {
  PathGlob() = default;
  explicit PathGlob(std::string path_pattern);

  bool match(const std::string& path) const;
};

}  // namespace a0
