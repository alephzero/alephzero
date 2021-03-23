#pragma once

#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
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
#include <vector>

#include "sync.hpp"
#include "transport_tools.hpp"
#include "unused.h"

#ifndef REQUIRE
#define REQUIRE(...)
#endif

#define REQUIRE_OK(err) REQUIRE((err) == A0_OK);

namespace a0::test {

inline a0_buf_t buf(a0_transport_frame_t frame) {
  return a0_buf_t{
      .ptr = frame.data,
      .size = frame.hdr.data_size,
  };
}

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.ptr, buf.size);
}

inline std::string str(a0_transport_frame_t frame) {
  return str(buf(frame));
}

inline a0_buf_t buf(std::string str) {
  static sync<std::set<std::string>> memory;
  return memory.with_lock([&](auto* mem) {
    auto [iter, did_insert] = mem->insert(std::move(str));
    A0_MAYBE_UNUSED(did_insert);
    return a0_buf_t{
        .ptr = (uint8_t*)iter->c_str(),
        .size = iter->size(),
    };
  });
}

inline a0_alloc_t alloc() {
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

inline a0_packet_t pkt(std::string payload) {
  a0_packet_t pkt_;
  a0_packet_init(&pkt_);
  pkt_.payload = a0::test::buf(std::move(payload));
  return pkt_;
}

inline a0_packet_t pkt(
    std::vector<std::pair<std::string, std::string>> hdrs,
    std::string payload) {
  static sync<std::vector<std::unique_ptr<std::vector<a0_packet_header_t>>>> memory;
  auto pkt_ = pkt(std::move(payload));

  auto pkt_hdrs = std::make_unique<std::vector<a0_packet_header_t>>();
  for (auto&& [k, v] : hdrs) {
    pkt_hdrs->push_back(a0_packet_header_t{
        .key = (char*)a0::test::buf(std::move(k)).ptr,
        .val = (char*)a0::test::buf(std::move(v)).ptr,
    });
  }
  pkt_.headers_block = {
      .headers = &pkt_hdrs->front(),
      .size = pkt_hdrs->size(),
      .next_block = nullptr,
  };
  memory.with_lock([&](auto* mem) {
    mem->push_back(std::move(pkt_hdrs));
  });
  return pkt_;
}

inline a0_packet_t pkt(a0_flat_packet_t fpkt) {
  a0_packet_t out;
  REQUIRE_OK(a0_packet_deserialize(fpkt, alloc(), &out));
  return out;
}

struct pkt_cmp_t {
  bool payload_match;
  bool content_match;
  bool full_match;
};
inline pkt_cmp_t pkt_cmp(a0_packet_t lhs, a0_packet_t rhs) {
  pkt_cmp_t ret;
  ret.payload_match = (str(lhs.payload) == str(rhs.payload));
  ret.content_match = ret.payload_match && [&]() {
    std::vector<std::pair<std::string, std::string>> lhs_hdrs;
    a0_packet_header_callback_t lhs_cb = {
        .user_data = &lhs_hdrs,
        .fn = [](void* user_data, a0_packet_header_t hdr) {
          auto* hdrs = (std::vector<std::pair<std::string, std::string>>*)user_data;
          hdrs->push_back({std::string(hdr.key), std::string(hdr.val)});
        },
    };
    REQUIRE_OK(a0_packet_for_each_header(lhs.headers_block, lhs_cb));

    std::vector<std::pair<std::string, std::string>> rhs_hdrs;
    a0_packet_header_callback_t rhs_cb = {
        .user_data = &rhs_hdrs,
        .fn = [](void* user_data, a0_packet_header_t hdr) {
          auto* hdrs = (std::vector<std::pair<std::string, std::string>>*)user_data;
          hdrs->push_back({std::string(hdr.key), std::string(hdr.val)});
        },
    };
    REQUIRE_OK(a0_packet_for_each_header(rhs.headers_block, rhs_cb));

    return lhs_hdrs == rhs_hdrs;
  }();
  ret.full_match = ret.content_match && (memcmp(lhs.id, rhs.id, sizeof(a0_uuid_t)) == 0);
  return ret;
}

template <typename C>
struct _c2cpp_ {
  C c;
  
};

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

#define Test_AutoC2CPP_start(STEM, CPPNAME)                        \
class CPPNAME {                                                    \
  CPPNAME() = default;                                             \
 public:                                                           \
  a0_##STEM##_t c;                                                 \
  template <typename... Args>                                      \
  explicit CPPNAME(Args&&... args) {                               \
    REQUIRE_OK(a0_##STEM##_init(&c, std::forward<Args>(args)...)); \
  }                                                                \
  CPPNAME(const CPPNAME&) = delete;                                \
  CPPNAME(CPPNAME&&) = default;                                    \
  ~CPPNAME() { a0_##STEM##_close(&c); }                            \
  static CPPNAME from_c(a0_##STEM##_t c) {                         \
    CPPNAME cpp;                                                   \
    cpp.c = c;                                                     \
    return cpp;                                                    \
  }

#define Test_AutoC2CPP_end(STEM) \
};

#define Test_AutoC2CPP_method(STEM, FN)                          \
template <typename... Args>                                      \
void FN(Args&&... args) {                                        \
  REQUIRE_OK(a0_##STEM##_##FN(&c, std::forward<Args>(args)...)); \
}

#define Test_AutoC2CPP_method_ret(STEM, FN, RETURN_T)                  \
template <typename... Args>                                            \
RETURN_T FN(Args&&... args) {                                          \
  RETURN_T ret;                                                        \
  REQUIRE_OK(a0_##STEM##_##FN(&c, std::forward<Args>(args)..., &ret)); \
  return ret;                                                          \
}

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
    REQUIRE_SUBPROC_EXITED(a0::test::subproc([&]() FN_BODY)); \
  }

#define REQUIRE_SIGNAL(FN_BODY)                                 \
  {                                                             \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */            \
    REQUIRE_SUBPROC_SIGNALED(a0::test::subproc([&]() FN_BODY)); \
  }
