#pragma once

#include <a0/common.h>

#include <a0/internal/stream_tools.hh>

#include <map>
#include <mutex>
#include <set>
#include <string>

namespace a0 {
namespace test {

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}

inline std::string str(a0_stream_frame_t frame) {
  return str(a0::buf(frame));
}

inline a0_buf_t buf(std::string str) {
  static std::set<std::string> mem;
  static std::mutex mu;
  std::unique_lock<std::mutex> lk{mu};
  if (!mem.count(str)) {
    mem.insert(str);
  }
  return a0_buf_t{
      .ptr = (uint8_t*)mem.find(str)->c_str(),
      .size = str.size(),
  };
}

inline a0_alloc_t allocator() {
  static struct data_t {
    std::map<size_t, std::string> dump;
    std::mutex mu;
  } data;

  return (a0_alloc_t){
      .user_data = &data,
      .fn =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* data = (data_t*)user_data;
            std::unique_lock<std::mutex> lk{data->mu};
            auto key = data->dump.size();
            data->dump[key].resize(size);
            out->size = size;
            out->ptr = (uint8_t*)data->dump[key].c_str();
          },
  };
};

inline bool is_valgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#endif
  char* env = getenv("RUNNING_ON_VALGRIND");
  return env && std::string(env) != "0";
}

}  // namespace test
}  // namespace a0
