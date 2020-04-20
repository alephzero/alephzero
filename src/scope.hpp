#pragma once

#include <functional>

namespace a0 {

template <typename T>
class scope {
  T val;
  std::function<void(T*)> deleter;

 public:
  scope(T&& val_, std::function<void(T*)> deleter_)
      : val{std::forward<T>(val_)}, deleter{std::move(deleter_)} {}

  scope(T&& val_, std::function<void(T)> deleter_)
      : val{std::forward<T>(val_)}, deleter{[del = std::move(deleter_)](T* t) {
          del(std::move(*t));
        }} {}

  scope(const scope&) = delete;
  scope& operator=(const scope&) = delete;

  ~scope() {
    deleter(&val);
  }

  T& operator*() noexcept {
    return val;
  }
  const T& operator*() const noexcept {
    return val;
  }

  T* operator->() noexcept {
    return &val;
  }
  const T* operator->() const noexcept {
    return &val;
  }
};

template <>
class scope<void> {
  std::function<void()> atexit;

 public:
  scope(std::function<void()> atexit_) : atexit{std::move(atexit_)} {}

  scope(const scope&) = delete;
  scope& operator=(const scope&) = delete;

  ~scope() {
    atexit();
  }
};

}  // namespace a0
