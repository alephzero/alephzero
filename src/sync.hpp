#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace a0 {

template <typename...>
constexpr std::false_type INVALID_SYNC_FUNCTION{};

struct monitor {
  std::shared_mutex mu;
  std::condition_variable_any cv;

  void lock() { mu.lock(); }
  void try_lock() { mu.try_lock(); }
  void unlock() { mu.unlock(); }

  void lock_shared() { mu.lock_shared(); }
  void try_lock_shared() { mu.try_lock_shared(); }
  void unlock_shared() { mu.unlock_shared(); }

  void notify_one() noexcept {
    cv.notify_one();
  }

  void notify_all() noexcept {
    cv.notify_all();
  }

  void wait() {
    cv.wait(mu);
  }

  template <typename Pred>
  void wait(Pred&& pred) {
    cv.wait(mu, std::forward<Pred>(pred));
  }

  template <typename Rep, typename Period>
  std::cv_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return cv.wait_for(mu, rel_time);
  }

  template <typename Rep, typename Period, typename Pred>
  bool wait_for(const std::chrono::duration<Rep, Period>& rel_time, Pred&& pred) {
    return cv.wait_for(mu, rel_time, std::forward<Pred>(pred));
  }

  template <typename Clock, typename Duration>
  std::cv_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
    return cv.wait_for(mu, timeout_time);
  }

  template <typename Clock, typename Duration, typename Pred>
  bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time, Pred&& pred) {
    return cv.wait_until(mu, timeout_time, std::forward<Pred>(pred));
  }
};

template <typename T>
class sync {
  T val;
  mutable monitor mon;

  template <typename Fn>
  auto invoke(Fn&& fn) {
    if constexpr (std::is_invocable_v<Fn, T> ||
                  std::is_invocable_v<Fn, T&>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (std::is_invocable_v<Fn, monitor*, T> ||
                         std::is_invocable_v<Fn, monitor*, T&>) {
      return fn(&mon, val);
    } else if constexpr (std::is_invocable_v<Fn, monitor*, T*>) {
      return fn(&mon, &val);
    } else if constexpr (std::is_invocable_v<Fn, monitor*>) {
      return fn(&mon);
    } else {
      static_assert(INVALID_SYNC_FUNCTION<Fn>);
    }
  }

  template <typename Fn>
  auto invoke(Fn&& fn) const {
    if constexpr (std::is_invocable_v<Fn, T> ||
                  std::is_invocable_v<Fn, const T&>) {
      return fn(val);
    } else if constexpr (std::is_invocable_v<Fn, const T*>) {
      return fn(&val);
    } else if constexpr (std::is_invocable_v<Fn>) {
      return fn();
    } else if constexpr (std::is_invocable_v<Fn, monitor*, T> ||
                         std::is_invocable_v<Fn, monitor*, const T&>) {
      return fn(&mon, val);
    } else if constexpr (std::is_invocable_v<Fn, monitor*, const T*>) {
      return fn(&mon, &val);
    } else if constexpr (std::is_invocable_v<Fn, monitor*>) {
      return fn(&mon);
    } else {
      static_assert(INVALID_SYNC_FUNCTION<Fn>);
    }
  }

  template <typename Fn>
  auto bind(Fn&& fn) {
    return [this, fn = std::forward<Fn>(fn)]() {
      return invoke(fn);
    };
  }

  template <typename Fn>
  auto bind(Fn&& fn) const {
    return [this, fn = std::forward<Fn>(fn)]() {
      return invoke(fn);
    };
  }

 public:
  template <typename... Args>
  explicit sync(Args&&... args)
      : val(std::forward<Args>(args)...) {}

  template <typename Fn>
  auto with_lock(Fn&& fn) {
    std::unique_lock<monitor> lk{mon};
    return invoke(std::forward<Fn>(fn));
  }

  template <typename Fn>
  auto with_shared_lock(Fn&& fn) const {
    std::shared_lock<monitor> lk{mon};
    return invoke(std::forward<Fn>(fn));
  }

  template <typename U>
  void set(U&& new_val) {
    with_lock([&](T& t) {
      t = std::forward<U>(new_val);
    });
  }

  T copy() const {
    return with_shared_lock([](const T& t) {
      return t;
    });
  }

  T&& release() {
    return with_lock([](T& val) {
      return std::move(val);
    });
  }

  template <typename Fn>
  void wait(Fn&& fn) {
    with_lock([&]() {
      mon.wait(bind(std::forward<Fn>(fn)));
    });
  }

  template <typename Rep, typename Period>
  std::cv_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return with_lock([&]() {
      return mon.wait_for(rel_time);
    });
  }

  template <typename Rep, typename Period, typename Fn>
  bool wait_for(const std::chrono::duration<Rep, Period>& rel_time, Fn&& fn) {
    return with_lock([&]() {
      return mon.wait_for(rel_time, bind(std::forward<Fn>(fn)));
    });
  }

  template <typename Clock, typename Duration>
  std::cv_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
    return with_lock([&]() {
      return mon.wait_for(timeout_time);
    });
  }

  template <typename Clock, typename Duration, typename Fn>
  bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time, Fn&& fn) {
    return with_lock([&]() {
      return mon.wait_until(timeout_time, bind(std::forward<Fn>(fn)));
    });
  }

  template <typename Fn>
  void shared_wait(Fn&& fn) const {
    with_shared_lock([&]() {
      mon.wait(bind(std::forward<Fn>(fn)));
    });
  }

  template <typename Rep, typename Period>
  std::cv_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const {
    return with_shared_lock([&]() {
      return mon.wait_for(rel_time);
    });
  }

  template <typename Rep, typename Period, typename Fn>
  bool wait_for(const std::chrono::duration<Rep, Period>& rel_time, Fn&& fn) const {
    return with_shared_lock([&]() {
      return mon.wait_for(rel_time, bind(std::forward<Fn>(fn)));
    });
  }

  template <typename Clock, typename Duration>
  std::cv_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const {
    return with_shared_lock([&]() {
      return mon.wait_for(timeout_time);
    });
  }

  template <typename Clock, typename Duration, typename Fn>
  bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time, Fn&& fn) const {
    return with_shared_lock([&]() {
      return mon.wait_until(timeout_time, bind(std::forward<Fn>(fn)));
    });
  }

  auto notify_one() {
    mon.notify_one();
  }

  template <typename Fn>
  auto notify_one(Fn&& fn) {
    with_lock([&]() {
      mon.notify_one();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto shared_notify_one() const {
    mon.notify_one();
  }

  template <typename Fn>
  auto shared_notify_one(Fn&& fn) const {
    with_shared_lock([&]() {
      mon.notify_one();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto notify_all() {
    mon.notify_all();
  }

  template <typename Fn>
  auto notify_all(Fn&& fn) {
    with_lock([&]() {
      mon.notify_all();
      return invoke(std::forward<Fn>(fn));
    });
  }

  auto shared_notify_all() const {
    mon.notify_all();
  }

  template <typename Fn>
  auto shared_notify_all(Fn&& fn) const {
    with_shared_lock([&]() {
      mon.notify_all();
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

  template <typename Rep, typename Period>
  std::cv_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const {
    if (evt.wait_for(rel_time, [](bool b) {
          return b;
        })) {
      return std::cv_status::no_timeout;
    }
    return std::cv_status::timeout;
  }

  template <typename Clock, typename Duration>
  std::cv_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const {
    if (evt.wait_until(timeout_time, [](bool b) {
          return b;
        })) {
      return std::cv_status::no_timeout;
    }
    return std::cv_status::timeout;
  }

  bool is_set() const {
    return evt.copy();
  }

  void set() {
    evt.notify_all([](bool& ready) {
      ready = true;
    });
  }

  void clear() {
    evt.notify_all([](bool& ready) {
      ready = false;
    });
  }
};

}  // namespace a0
