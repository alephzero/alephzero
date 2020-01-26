#pragma once

#include <shared_mutex>

namespace a0 {

// Type wrapper the requires all access to the underlying object be thread-safe.
template <typename T>
class sync {
  T val;
  mutable std::shared_mutex mu;
  mutable std::condition_variable_any cv;

  int constexpr_fail(int x) {
    return x;
  }

  template <typename Fn>
  auto invoke(Fn&& fn) {
    if constexpr (std::is_invocable_v<Fn, T> || std::is_invocable_v<Fn, T&>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (true) {
      constexpr_fail(0);
    }
  }

  template <typename Fn>
  auto invoke(Fn&& fn) const {
    if constexpr (std::is_invocable_v<Fn, T>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, const T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (true) {
      constexpr_fail(0);
    }
  }

 public:
  template <typename... Args>
  sync(Args&&... args) : val(std::forward<Args>(args)...) {}

  template <typename Fn>
  auto with_lock(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    return invoke(std::forward<Fn>(fn));
  }

  template <typename Fn>
  auto with_shared_lock(Fn&& fn) const {
    std::shared_lock<std::shared_mutex> lk{mu};
    return invoke(std::forward<Fn>(fn));
  }

  template <typename U>
  void set(U&& new_val) {
    with_lock([&](T* t) { *t = std::forward<U>(new_val); });
  }

  T copy() const {
    return with_shared_lock([](const T& t) { return t; });
  }

  template <typename Fn0, typename Fn1>
  auto wait(Fn0&& fn0, Fn1&& fn1) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.wait(lk, [&]() { return invoke(std::forward<Fn0>(fn0)); });
    return invoke(std::forward<Fn1>(fn1));
  }

  template <typename Fn0>
  auto wait(Fn0&& fn0) {
    wait(std::forward<Fn0>(fn0), [](T*) {});
  }

  void wait() {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.wait(lk);
  }

  template <typename Fn0, typename Fn1>
  auto shared_wait(Fn0&& fn0, Fn1&& fn1) const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.wait(lk, [&]() { return invoke(std::forward<Fn0>(fn0)); });
    return invoke(std::forward<Fn1>(fn1));
  }

  template <typename Fn0>
  auto shared_wait(Fn0&& fn0) const {
    shared_wait(std::forward<Fn0>(fn0), [](const T&) {});
  }

  void shared_wait() const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.wait(lk);
  }

  template <typename Fn>
  auto notify_one(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.notify_one();
    return invoke(std::forward<Fn>(fn));
  }

  auto notify_one() {
    notify_one([](T*) {});
  }

  template <typename Fn>
  auto notify_all(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.notify_all();
    return invoke(std::forward<Fn>(fn));
  }

  auto notify_all() {
    notify_all([](T*) {});
  }
};

class Event {
  sync<bool> evt;

 public:
  void wait() const {
    evt.shared_wait([](bool ready) { return ready; });
  }

  bool is_set() const {
    return evt.with_shared_lock([](bool ready) { return ready; });
  }

  void set() {
    evt.notify_all([](bool* ready) { *ready = true; });
  }

  void clear() {
    evt.notify_all([](bool* ready) { *ready = false; });
  }
};

}  // namespace a0
