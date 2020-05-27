#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace a0 {

struct strutil {
  template <typename... Arg>
  static std::string cat(Arg&&... arg) {
    std::ostringstream ss;
    (void)(ss << ... << std::forward<Arg>(arg));
    return ss.str();
  }

  template <typename Container>
  static std::string join(Container c) {
    std::ostringstream ss;
    for (auto&& v : c) {
      ss << v;
    }
    return ss.str();
  }

  template <typename... Args>
  static std::string fmt(std::string_view format, Args... args) {
    size_t size = snprintf(nullptr, 0, format.data(), args...);
    std::vector<char> buf(size + 1);
    sprintf(buf.data(), format.data(), args...);
    return std::string(buf.data(), size);
  }
};

}  // namespace a0
