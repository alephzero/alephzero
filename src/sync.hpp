#pragma once

#include <functional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace a0 {

template <typename...>
constexpr std::false_type INVALID_SYNC_FUNCTION{};

template <typename T>
class sync {
  T val;
  mutable std::mutex mu;
  mutable std::shared_mutex sh_mu;
  mutable std::condition_variable_any cv;

  mutable std::unordered_map<std::thread::id, std::unique_lock<std::mutex>> unique_locked_threads;
  mutable std::unordered_map<std::thread::id, std::shared_lock<std::shared_mutex>> shared_locked_threads;

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

  void upgrade() {
    auto id = std::this_thread::get_id();
    unique_locked_threads[id] = std::unique_lock<std::mutex>{mu};
  }

  void downgrade() {
    auto id = std::this_thread::get_id();
    unique_locked_threads.erase(id);
  }

  struct guard_t {
    std::function<void()> fn;
    ~guard_t() { fn(); }
  };

 public:
  template <typename... Args>
  sync(Args&&... args) : val(std::forward<Args>(args)...) {}

  template <typename Fn>
  auto with_lock(Fn&& fn) {
    auto id = std::this_thread::get_id();
    if (unique_locked_threads.count(id)) {
      return invoke(std::forward<Fn>(fn));
    } else if (shared_locked_threads.count(id)) {
      upgrade();
      guard_t guard_upgrad{[&]() { downgrade(); }};
      return invoke(std::forward<Fn>(fn));
    } else {
      shared_locked_threads[id] = std::shared_lock<std::shared_mutex>{sh_mu};
      guard_t guard_share{[&]() { shared_locked_threads.erase(id); }};

      unique_locked_threads[id] = std::unique_lock<std::mutex>{mu};
      guard_t guard_uniqu{[&]() { unique_locked_threads.erase(id); }};

      return invoke(std::forward<Fn>(fn));
    }
  }

  template <typename Fn>
  auto with_shared_lock(Fn&& fn) const {
    auto id = std::this_thread::get_id();
    if (unique_locked_threads.count(id)) {
      return invoke(std::forward<Fn>(fn));
    } else if (shared_locked_threads.count(id)) {
      return invoke(std::forward<Fn>(fn));
    } else {
      shared_locked_threads[id] = std::shared_lock<std::shared_mutex>{sh_mu};
      guard_t guard_shared{[&]() { shared_locked_threads.erase(id); }};
      return invoke(std::forward<Fn>(fn));
    }
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
    shared_wait();
  }

  template <typename Fn>
  void wait(Fn&& fn) {
    return with_shared_lock([&]() {
      cv.wait(
        shared_locked_threads[std::this_thread::get_id()],
        [&]() {
          return with_lock([&]() {
            return invoke(std::forward<Fn>(fn));
          });
        });
    });
  }

  void shared_wait() const {
    return with_shared_lock([&]() {
      cv.wait(shared_locked_threads[std::this_thread::get_id()]);
    });
  }

  template <typename Fn>
  void shared_wait(Fn&& fn) const {
    return with_shared_lock([&]() {
      cv.wait(
        shared_locked_threads[std::this_thread::get_id()],
        [&]() { return invoke(std::forward<Fn>(fn)); });
    });
  }

  auto notify_one() {
    shared_notify_one();
  }

  template <typename Fn>
  auto notify_one(Fn&& fn) {
    return with_lock([&]() {
      cv.notify_one();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto shared_notify_one() const {
    cv.notify_one();
  }

  template <typename Fn>
  auto shared_notify_one(Fn&& fn) const {
    return with_shared_lock([&]() {
      cv.notify_one();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto notify_all() {
    shared_notify_all();
  }

  template <typename Fn>
  auto notify_all(Fn&& fn) {
    return with_lock([&]() {
      cv.notify_all();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto shared_notify_all() const {
    cv.notify_all();
  }

  template <typename Fn>
  auto shared_notify_all(Fn&& fn) const {
    return with_shared_lock([&]() {
      cv.notify_all();
      return invoke(std::forward<Fn>(fn));
    });
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
    return evt.copy();
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
