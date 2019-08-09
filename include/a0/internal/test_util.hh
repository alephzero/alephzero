#pragma once

#include <a0/common.h>

#include <string>

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}

inline bool is_valgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#endif
  char* env = getenv("RUNNING_ON_VALGRIND");
  return env && std::string(env) != "0";
}
