#pragma once

#include <a0/c_wrap.hpp>
#include <a0/err.h>

#include <chrono>
#include <functional>
#include <system_error>
#include <thread>
#include <vector>

namespace a0 {
namespace {

void check(errno_t err) {
  if (err) {
    throw std::system_error(err, std::generic_category());
  }
}

template <typename C>
struct CDeleter {
  std::function<void(C*)> primary;
  std::vector<std::function<void()>> also;

  CDeleter() = default;
  explicit CDeleter(std::function<void(C*)> primary)
      : primary{std::move(primary)} {}

  CDeleter(const CDeleter&) = delete;
  CDeleter(CDeleter&&) noexcept = default;
  CDeleter& operator=(const CDeleter&) = delete;
  CDeleter& operator=(CDeleter&&) noexcept = default;

  void operator()(C* c) {
    if (primary) {
      primary(c);
    }
    for (auto&& fn : also) {
      fn();
    }
    delete c;
  }
};

template <typename C, typename InitFn, typename Closer>
void set_c(std::shared_ptr<C>* c, InitFn&& init, Closer&& closer) {
  set_c(c, std::forward<InitFn>(init), CDeleter<C>(std::forward<Closer>(closer)));
}

template <typename C, typename InitFn>
void set_c(std::shared_ptr<C>* c, InitFn&& init, CDeleter<C> deleter) {
  *c = std::shared_ptr<C>(new C, std::move(deleter));
  errno_t err = init(c->get());
  if (err) {
    std::get_deleter<CDeleter<C>>(*c)->primary = nullptr;
    *c = nullptr;
    check(err);
  }
}

template <typename CPP, typename InitFn, typename Closer>
CPP make_cpp(InitFn&& init, Closer&& closer) {
  CPP cpp;
  set_c(&cpp.c, std::forward<InitFn>(init), std::forward<Closer>(closer));
  return cpp;
}

template <typename T>
void check(const std::string& fn_name, const details::CppWrap<T>* cpp_wrap) {
  if (!cpp_wrap || !cpp_wrap->c) {
    auto msg = std::string("AlephZero method called with NULL object: ") + fn_name;
    fprintf(stderr, "%s\n", msg.c_str());
    throw std::runtime_error(msg);
  }

  if (cpp_wrap->magic_number != 0xA0A0A0A0) {
    auto msg = std::string("AlephZero method called with corrupt object: ") + fn_name;
    fprintf(stderr, "%s\n", msg.c_str());
    // This error is often the result of a throw in another thread. Let that propagate first.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    throw std::runtime_error(msg);
  }
}

#define CHECK_C \
  check(__PRETTY_FUNCTION__, this)

template <typename T>
const T& as_const(T& t) noexcept {
    return t;
}
template <typename T>
const T* as_const(T* t) noexcept {
    return t;
}
template <typename T>
T& as_mutable(const T& t) noexcept {
    return const_cast<T&>(t);
}
template<typename T>
T* as_mutable(const T* t) noexcept {
    return const_cast<T*>(t);
}

template <typename T>
a0_buf_t as_buf(T& mem) {
  return a0_buf_t{
      .ptr = (uint8_t*)(mem.data()),
      .size = mem.size(),
  };
}

// TODO(lshamis): Is this the right response?
#define TRY(NAME, BODY)                                          \
  try {                                                          \
    BODY;                                                        \
  } catch (const std::exception& e) {                            \
    fprintf(stderr, NAME " threw an exception: %s\n", e.what()); \
    std::terminate();                                            \
  } catch (...) {                                                \
    fprintf(stderr, NAME " threw an exception: ???\n");          \
    std::terminate();                                            \
  }

}  // namespace
}  // namespace a0
