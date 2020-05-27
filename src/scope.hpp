#pragma once

#include <functional>
#include <utility>

namespace a0 {

template <typename T>
class scope {
  T val;
  std::function<void(T*)> deleter;

 public:
  template <typename U = T>
  scope(U&& val_, std::function<void(T*)> deleter_)
      : val{std::forward<U>(val_)}, deleter{std::move(deleter_)} {}

  template <typename U = T>
  scope(U&& val_, std::function<void(T)> deleter_)
      : val{std::forward<U>(val_)}, deleter{[del = std::move(deleter_)](T* t) {
          del(std::move(*t));
        }} {}

  scope(const scope&) = delete;
  scope& operator=(const scope&) = delete;

  ~scope() {
    deleter(&val);
  }

  T* get() noexcept {
    return &val;
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
  explicit scope(std::function<void()> atexit_)
      : atexit{std::move(atexit_)} {}

  scope(const scope&) = delete;
  scope& operator=(const scope&) = delete;

  ~scope() {
    atexit();
  }
};

}  // namespace a0
