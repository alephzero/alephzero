#pragma once

#include <a0/buf.h>
#include <a0/c_wrap.hpp>
#include <a0/err.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace a0 {
namespace {

void check(a0_err_t err) {
  if (err == A0_ERRCODE_SYSERR) {
    throw std::system_error(a0_err_syscode, std::generic_category());
  } else if (err) {
    throw std::runtime_error(a0_strerror(err));
  }
}

template <typename C, typename Impl>
struct CDeleter {
  std::unique_ptr<Impl> impl;
  std::function<void(C*, Impl*)> closer;

  void operator()(C* c) {
    closer(c, impl.get());
    closer = nullptr;
    impl = nullptr;
    delete c;
  }
};

template <typename Impl, typename C, typename InitFn, typename Closer>
void set_c_impl(std::shared_ptr<C>* c, InitFn&& init, Closer&& closer) {
  auto unique_c = std::unique_ptr<C>(new C);
  auto unique_impl = std::unique_ptr<Impl>(new Impl);
  check(init(unique_c.get(), unique_impl.get()));

  *c = std::shared_ptr<C>(unique_c.release(), CDeleter<C, Impl>{
                                                  std::move(unique_impl),
                                                  [closer](C* c, Impl* impl) { closer(c, impl); },
                                              });
}

template <typename Impl, typename C, typename InitFn>
void set_c_impl(std::shared_ptr<C>* c, InitFn&& init) {
  set_c_impl<Impl>(c, std::forward<InitFn>(init), [](C*, Impl*) {});
}

template <typename C, typename InitFn, typename Closer>
void set_c(std::shared_ptr<C>* c, InitFn&& init, Closer&& closer) {
  set_c_impl<int>(
      c,
      [init](C* c, int*) { return init(c); },
      [closer](C* c, int*) { closer(c); });
}

template <typename C, typename InitFn>
void set_c(std::shared_ptr<C>* c, InitFn&& init) {
  set_c(c, std::forward<InitFn>(init), [](C*) {});
}

template <typename Impl, typename C>
Impl* c_impl(std::shared_ptr<C>* c) {
  return std::get_deleter<CDeleter<C, Impl>>(*c)->impl.get();
}

template <typename Impl, typename C>
const Impl* c_impl(const std::shared_ptr<C>* c) {
  return std::get_deleter<CDeleter<C, Impl>>(*c)->impl.get();
}

template <typename CPP, typename Impl, typename InitFn, typename Closer>
CPP make_cpp_impl(InitFn&& init, Closer&& closer) {
  CPP cpp;
  set_c_impl<Impl>(&cpp.c, std::forward<InitFn>(init), std::forward<Closer>(closer));
  return cpp;
}

template <typename CPP, typename InitFn, typename Closer>
CPP make_cpp(InitFn&& init, Closer&& closer) {
  CPP cpp;
  set_c(&cpp.c, std::forward<InitFn>(init), std::forward<Closer>(closer));
  return cpp;
}

template <typename CPP, typename Impl, typename InitFn>
CPP make_cpp_impl(InitFn&& init) {
  using c_type = typename CPP::c_type;
  return make_cpp_impl<CPP, Impl>(std::forward<InitFn>(init), [](c_type*, Impl*) {});
}

template <typename CPP, typename InitFn>
CPP make_cpp(InitFn&& init) {
  using c_type = typename CPP::c_type;
  return make_cpp<CPP>(std::forward<InitFn>(init), [](c_type*) {});
}

template <typename CPP, typename Impl>
CPP make_cpp_impl() {
  using c_type = typename CPP::c_type;
  return make_cpp_impl<CPP, Impl>([](c_type*) { return A0_OK; }, [](c_type*) {});
}

template <typename CPP>
CPP make_cpp() {
  using c_type = typename CPP::c_type;
  return make_cpp<CPP>([](c_type*) { return A0_OK; }, [](c_type*) {});
}

template <typename CPP>
CPP cpp_wrap(typename CPP::c_type c) {
  using c_type = typename CPP::c_type;
  return make_cpp<CPP>(
      [&](c_type* c_) {
        *c_ = c;
        return A0_OK;
      },
      [](c_type*) {});
}

template <typename T>
void check(const std::string& fn_name, const details::CppWrap<T>* cpp_obj) {
  if (!cpp_obj || !cpp_obj->c) {
    auto msg = std::string("AlephZero method called with NULL object: ") + fn_name;
    fprintf(stderr, "%s\n", msg.c_str());
    throw std::runtime_error(msg);
  }

  if (cpp_obj->magic_number != 0xA0A0A0A0) {
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
template <typename T>
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
