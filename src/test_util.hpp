#pragma once

#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/transport.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef REQUIRE
#define REQUIRE(...)
#endif

#define REQUIRE_OK(err) REQUIRE((err) == A0_OK)

namespace a0 {
namespace test {

template <typename... Args>
static std::string fmt(const std::string& format, Args... args) {
  size_t size = snprintf(nullptr, 0, format.data(), args...);
  std::vector<char> buf(size + 1);
  sprintf(buf.data(), format.data(), args...);
  return std::string(buf.data(), size);
}

inline std::string random_ascii_string(size_t length) {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

inline a0_buf_t buf(a0_transport_frame_t frame) {
  return a0_buf_t{frame.data, frame.hdr.data_size};
}

inline std::string str(a0_buf_t buf) {
  return std::string((char*)buf.data, buf.size);
}

inline std::string str(a0_transport_frame_t frame) {
  return str(buf(frame));
}

inline a0_buf_t buf(std::string str) {
  static struct {
    std::mutex mu;
    std::set<std::string> mem;
  } data{};
  std::unique_lock<std::mutex> lk{data.mu};

  auto result = data.mem.insert(std::move(str));
  return a0_buf_t{(uint8_t*)result.first->c_str(), result.first->size()};
}

inline a0_alloc_t alloc() {
  static struct data_t {
    std::mutex mu;
    std::map<size_t, std::string> dump;
  } data{};

  return (a0_alloc_t){
      .user_data = &data,
      .alloc =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* data = (data_t*)user_data;
            std::unique_lock<std::mutex> lk{data->mu};

            auto key = data->dump.size();
            data->dump[key].resize(size);
            out->size = size;
            out->data = (uint8_t*)(data->dump[key].c_str());
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

inline a0_packet_t pkt(a0_buf_t payload) {
  return pkt(str(payload));
}

inline a0_packet_t pkt(
    std::vector<std::pair<std::string, std::string>> hdrs,
    std::string payload) {
  static struct {
    std::mutex mu;
    std::vector<std::unique_ptr<std::vector<a0_packet_header_t>>> mem;
  } data{};

  auto pkt_ = pkt(std::move(payload));
  std::unique_ptr<std::vector<a0_packet_header_t>> pkt_hdrs(new std::vector<a0_packet_header_t>);
  for (auto&& elem : hdrs) {
    auto&& k = elem.first;
    auto&& v = elem.second;
    pkt_hdrs->push_back(a0_packet_header_t{
        .key = (char*)a0::test::buf(std::move(k)).data,
        .val = (char*)a0::test::buf(std::move(v)).data,
    });
  }
  pkt_.headers_block = {
      .headers = &pkt_hdrs->front(),
      .size = pkt_hdrs->size(),
      .next_block = nullptr,
  };

  std::unique_lock<std::mutex> lk{data.mu};
  data.mem.push_back(std::move(pkt_hdrs));
  return pkt_;
}

inline a0_packet_t unflatten(a0_flat_packet_t fpkt) {
  a0_packet_t out;
  a0_buf_t unused;
  REQUIRE_OK(a0_packet_deserialize(fpkt, alloc(), &out, &unused));
  return out;
}

inline std::unordered_multimap<std::string, std::string> hdr(a0_packet_t pkt) {
  std::unordered_multimap<std::string, std::string> result;

  a0_packet_header_iterator_t iter;
  a0_packet_header_t hdr;

  REQUIRE_OK(a0_packet_header_iterator_init(&iter, &pkt));
  while (a0_packet_header_iterator_next(&iter, &hdr) == A0_OK) {
    result.insert({std::string(hdr.key), std::string(hdr.val)});
  }

  return result;
}

inline std::unordered_multimap<std::string, std::string> hdr(a0_flat_packet_t fpkt) {
  std::unordered_multimap<std::string, std::string> result;

  a0_flat_packet_header_iterator_t iter;
  a0_packet_header_t hdr;

  REQUIRE_OK(a0_flat_packet_header_iterator_init(&iter, &fpkt));
  while (a0_flat_packet_header_iterator_next(&iter, &hdr) == A0_OK) {
    result.insert({std::string(hdr.key), std::string(hdr.val)});
  }

  return result;
}

struct pkt_cmp_t {
  bool payload_match;
  bool content_match;
  bool full_match;
};
inline pkt_cmp_t pkt_cmp(a0_packet_t lhs, a0_packet_t rhs) {
  pkt_cmp_t ret;
  ret.payload_match = (str(lhs.payload) == str(rhs.payload));
  ret.content_match = ret.payload_match && (hdr(lhs) == hdr(rhs));
  ret.full_match = ret.content_match && (std::string(lhs.id) == std::string(rhs.id));
  return ret;
}

class Latch {
  int32_t val;
  std::mutex mu;
  std::condition_variable cv;

 public:
  explicit Latch(int32_t init_val)
      : val{init_val} {}

  void count_down(int32_t update = 1) {
    std::unique_lock<std::mutex> lk{mu};
    val -= update;
    if (val <= 0) {
      cv.notify_all();
    }
  }

  void wait() {
    std::unique_lock<std::mutex> lk{mu};
    cv.wait(lk, [&]() { return val <= 0; });
  }

  void arrive_and_wait(int32_t update = 1) {
    std::unique_lock<std::mutex> lk{mu};
    val -= update;
    if (val <= 0) {
      cv.notify_all();
    }
    cv.wait(lk, [&]() { return val <= 0; });
  }
};

class Event {
  bool val{false};
  std::mutex mu;
  std::condition_variable cv;

 public:
  bool is_set() {
    std::unique_lock<std::mutex> lk{mu};
    return val;
  }

  void set() {
    std::unique_lock<std::mutex> lk{mu};
    val = true;
    cv.notify_all();
  }

  void wait() {
    std::unique_lock<std::mutex> lk{mu};
    cv.wait(lk, [&]() { return val; });
  }
};

class scope_env {
  std::string name;
  bool has_orig_val{false};
  std::string orig_val;

 public:
  scope_env(std::string name, std::string val)
      : name{name} {
    const char* orig_val_c = getenv(name.c_str());
    if (orig_val_c) {
      has_orig_val = true;
      orig_val = orig_val_c;
    }
    setenv(name.c_str(), val.c_str(), true);
  }

  ~scope_env() {
    if (has_orig_val) {
      setenv(name.c_str(), orig_val.c_str(), true);
    } else {
      unsetenv(name.c_str());
    }
  }
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
    signal(SIGSEGV, SIG_DFL);
    fn();
    exit(0);
  }
  return pid;
}

}  // namespace test
}  // namespace a0

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
