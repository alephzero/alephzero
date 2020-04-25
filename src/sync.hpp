#pragma once

#include <shared_mutex>

namespace a0 {

template <typename...>
constexpr std::false_type INVALID_SYNC_FUNCTION{};

template <typename T>
class sync {
  T val;
  mutable std::shared_mutex mu;
  mutable std::condition_variable_any cv;

  template <typename Fn>
  auto invoke(Fn&& fn) {
    if constexpr (std::is_invocable_v<Fn, T> || std::is_invocable_v<Fn, T&>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (true) {
      static_assert(INVALID_SYNC_FUNCTION<Fn>);
    }
  }

  template <typename Fn>
  auto invoke(Fn&& fn) const {
    if constexpr (std::is_invocable_v<Fn, T> || std::is_invocable_v<Fn, const T&>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, const T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (true) {
      static_assert(INVALID_SYNC_FUNCTION<Fn>);
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
    with_lock([&](T* t) {
      *t = std::forward<U>(new_val);
    });
  }

  T copy() const {
    return with_shared_lock([](const T& t) {
      return t;
    });
  }

  T&& release() {
    return with_lock([](T* val) {
      return std::move(*val);
    });
  }

  void wait() {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.wait(lk);
  }

  template <typename Fn>
  void wait(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.wait(lk, [&]() { return invoke(std::forward<Fn>(fn)); });
  }

  void shared_wait() const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.wait(lk);
  }

  template <typename Fn>
  void shared_wait(Fn&& fn) const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.wait(lk, [&]() { return invoke(std::forward<Fn>(fn)); });
  }

  auto notify_one() {
    cv.notify_one();
  }

  template <typename Fn>
  auto notify_one(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.notify_one();
    return invoke(std::forward<Fn>(fn));
  }

  auto shared_notify_one() const {
    cv.notify_one();
  }

  template <typename Fn>
  auto shared_notify_one(Fn&& fn) const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.notify_one();
    return invoke(std::forward<Fn>(fn));
  }

  auto notify_all() {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.notify_all();
  }

  template <typename Fn>
  auto notify_all(Fn&& fn) {
    std::unique_lock<std::shared_mutex> lk{mu};
    cv.notify_all();
    return invoke(std::forward<Fn>(fn));
  }

  auto shared_notify_all() const {
    cv.notify_all();
  }

  template <typename Fn>
  auto shared_notify_all(Fn&& fn) const {
    std::shared_lock<std::shared_mutex> lk{mu};
    cv.notify_all();
    return invoke(std::forward<Fn>(fn));
  }
};

class Event {
  sync<bool> evt;

 public:
  void wait() const {
    evt.shared_wait([](bool ready) {
      return ready;
    });
  }

  bool is_set() const {
    return evt.with_shared_lock([](bool ready) {
      return ready;
    });
  }

  void set() {
    evt.notify_all([](bool* ready) {
      *ready = true;
    });
  }

  void clear() {
    evt.notify_all([](bool* ready) {
      *ready = false;
    });
  }
};

}  // namespace a0
