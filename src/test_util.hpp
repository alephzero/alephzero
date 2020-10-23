#pragma once

#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/transport.h>

#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "sync.hpp"
#include "transport_tools.hpp"

namespace a0::test {

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}

inline std::string str(a0_transport_frame_t frame) {
  return str(a0::buf(frame));
}

inline a0_buf_t buf(std::string str) {
  static sync<std::set<std::string>> memory;
  return memory.with_lock([&](auto* mem) {
    if (!mem->count(str)) {
      mem->insert(str);
    }
    return a0_buf_t{
        .ptr = (uint8_t*)mem->find(str)->c_str(),
        .size = str.size(),
    };
  });
}

inline a0_alloc_t allocator() {
  static sync<std::map<size_t, std::string>> data;

  return (a0_alloc_t){
      .user_data = &data,
      .alloc =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* data = (sync<std::map<size_t, std::string>>*)user_data;
            data->with_lock([&](auto* dump) {
              auto key = dump->size();
              (*dump)[key].resize(size);
              out->size = size;
              out->ptr = (uint8_t*)((*dump)[key].c_str());
            });
            return A0_OK;
          },
      .dealloc = nullptr,
  };
};

inline a0_packet_headers_block_t header_block(a0_packet_header_t* hdr) {
  return a0_packet_headers_block_t{
      .headers = hdr,
      .size = 1,
      .next_block = nullptr,
  };
}

inline bool is_valgrind() {
#ifdef RUNNING_ON_VALGRIND
  return RUNNING_ON_VALGRIND;
#endif
  char* env = getenv("RUNNING_ON_VALGRIND");
  return env && std::string(env) != "0";
}

inline bool is_debug_mode() {
#ifdef DEBUG
  return true;
#else
  return false;
#endif
}

template <typename Fn>
inline pid_t subproc(Fn&& fn) {
  pid_t pid = fork();
  if (!pid) {
    /* Unhook doctest from the subprocess. */
    /* Otherwise, we would see a test-failure printout after the crash. */
    signal(SIGABRT, SIG_DFL);
    fn();
    exit(0);
  }
  return pid;
}

}  // namespace a0::test

#define REQUIRE_OK(err) REQUIRE((err) == A0_OK);
#define REQUIRE_EXIT(FN_BODY)                                 \
  {                                                           \
    int _ret_code;                                            \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */          \
    waitpid(a0::test::subproc([&]() FN_BODY), &_ret_code, 0); \
    REQUIRE(WIFEXITED(_ret_code));                            \
  }

#define REQUIRE_SIGNAL(FN_BODY)                               \
  {                                                           \
    int _ret_code;                                            \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */          \
    waitpid(a0::test::subproc([&]() FN_BODY), &_ret_code, 0); \
    REQUIRE(WIFSIGNALED(_ret_code));                          \
  }

#ifdef DEBUG

#define REQUIRE_SIGNAL_OR(EXPR, ERR) REQUIRE_SIGNAL({ EXPR; })

#else

#define REQUIRE_SIGNAL_OR(EXPR, ERR) REQUIRE((EXPR) == (ERR))

#endif
