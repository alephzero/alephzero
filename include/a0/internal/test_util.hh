#pragma once

#include <a0/common.h>

#include <set>
#include <string>

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}

inline a0_buf_t buf(std::string str) {
  static std::set<std::string> mem;
  if (!mem.count(str)) {
    mem.insert(str);
  }
  return a0_buf_t{
      .ptr = (uint8_t*)mem.find(str)->c_str(),
      .size = str.size(),
  };
}

inline bool is_valgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#endif
  char* env = getenv("RUNNING_ON_VALGRIND");
  return env && std::string(env) != "0";
}
