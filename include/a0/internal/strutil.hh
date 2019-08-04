#pragma once

#include <memory>
#include <sstream>

namespace a0 {

struct strutil {
  template <typename... Arg>
  static std::string cat(Arg&&... arg) {
    std::ostringstream ss;
    ((ss << std::forward<Arg>(arg)), ...);
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
  static std::string fmt(const std::string& format, Args... args) {
    size_t size = snprintf(nullptr, 0, format.c_str(), args...);
    std::unique_ptr<char[]> buf(new char[size]);
    sprintf(buf.get(), format.c_str(), args...);
    return std::string(buf.get(), size);
  }
};

}  // namespace a0