#include <a0/pathglob.h>
#include <a0/pathglob.hpp>

#include <memory>
#include <string>
#include <utility>

#include "c_wrap.hpp"

namespace a0 {

PathGlob::PathGlob(std::string path_pattern) {
  set_c(
      &c,
      [&](a0_pathglob_t* c) {
        return a0_pathglob_init(c, path_pattern.c_str());
      },
      a0_pathglob_close);
}

bool PathGlob::match(const std::string& path) const {
  CHECK_C;
  bool result;
  check(a0_pathglob_match(&*c, path.c_str(), &result));
  return result;
}

}  // namespace a0
