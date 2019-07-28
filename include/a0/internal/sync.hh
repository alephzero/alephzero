#pragma once

#include <functional>
#include <shared_mutex>

namespace a0 {

template <typename T>
class sync {
 private:
  T t_;
  mutable std::shared_mutex mu_;

 public:
  template <typename... Args>
  sync(Args&& ... args) : t_(std::forward<Args>(args)...) {}

  template <typename Fn>
  auto with_unique_lock(Fn&& fn) {
    static_assert(std::is_convertible_v<Fn, std::function<decltype(fn(t_))(T&)>>);
    std::unique_lock lk(mu_);
    return fn(t_);
  }

  template <typename Fn>
  auto with_shared_lock(Fn&& fn) const {
    static_assert(std::is_convertible_v<Fn, std::function<decltype(fn(t_))(const T&)>>);
    std::shared_lock lk(mu_);
    return fn(t_);
  }
};

}  // namespace a0
