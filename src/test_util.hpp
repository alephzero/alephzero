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
pid_t subproc(Fn&& fn) {
  pid_t pid = fork();
  if (pid == -1) {
    return pid;
  }
  if (!pid) {
    // Unhook doctest from the subprocess.
    // Otherwise, we would see a test-failure printout after the crash.
    signal(SIGABRT, SIG_DFL);
    fn();
    exit(0);
  }
  return pid;
}

}  // namespace a0::test

#define REQUIRE_OK(err) REQUIRE((err) == A0_OK);

inline void REQUIRE_SUBPROC_EXITED(pid_t pid) {
  REQUIRE(pid != -1);
  int ret_code;
  waitpid(pid, &ret_code, 0);
  REQUIRE(WIFEXITED(ret_code));
}

inline void REQUIRE_SUBPROC_SIGNALED(pid_t pid) {
  REQUIRE(pid != -1);
  int ret_code;
  waitpid(pid, &ret_code, 0);
  REQUIRE(WIFSIGNALED(ret_code));
}

#define REQUIRE_EXIT(FN_BODY)                                 \
  {                                                           \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */          \
    REQUIRE_SUBPROC_EXITED(a0::test::subproc([&]() FN_BODY));   \
  }

#define REQUIRE_SIGNAL(FN_BODY)                               \
  {                                                           \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */          \
    REQUIRE_SUBPROC_SIGNALED(a0::test::subproc([&]() FN_BODY));   \
  }

#ifdef DEBUG

#define REQUIRE_SIGNAL_OR(EXPR, ERR) REQUIRE_SIGNAL({ EXPR; })

#else

#define REQUIRE_SIGNAL_OR(EXPR, ERR) REQUIRE((EXPR) == (ERR))

#endif

#ifdef A0_TSAN_ENABLED
  // #ifdef __cplusplus
  // extern "C" {
  // #endif
    // void __tsan_mutex_destroy(void*, unsigned);
    // namespace __tsan {
    //   struct ThreadState;
    //   ThreadState* cur_thread();
    //   void MutexRepair(ThreadState*, uintptr_t, uintptr_t);
    // }  // namespace __tsan

    // void A0_MUTEX_REPAIR(void*) {
    // //   // __tsan::MutexRepair(__tsan::cur_thread(), 0, (uintptr_t)mtx);
    // }
    
  // #ifdef __cplusplus
  // }  // extern "C"
  // #endif

  #define A0_TSAN_MUTEX_DONE(mtx_ptr)
#else
  #define A0_TSAN_MUTEX_DONE(mtx_ptr)
  // void A0_MUTEX_REPAIR(void*) {}
#endif
