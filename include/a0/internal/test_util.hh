#pragma once

#include <a0/common.h>

#include <string>

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}
