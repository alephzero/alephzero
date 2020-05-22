#include <doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <type_traits>
#include <vector>

#include "src/sync.hpp"
#include "src/test_util.hpp"

// Note: This is not meant to be runnable code. It will block forever.
// The commented out lines will (and should) cause compilation errors.
void ensure_compiles() {
  struct ABC {};
  struct DEF {};
  a0::sync<ABC> abc;

  // abc.with_lock();
  abc.with_lock([]() {});
  abc.with_lock([](ABC&) {});
  abc.with_lock([](const ABC&) {});
  abc.with_lock([](ABC*) {});
  abc.with_lock([](const ABC*) {});
  // abc.with_lock([](DEF&) {});

  // abc.with_shared_lock();
  abc.with_shared_lock([]() {});
  abc.with_shared_lock([](ABC) {});
  // abc.with_shared_lock([](ABC&) {});
  abc.with_shared_lock([](const ABC&) {});
  // abc.with_shared_lock([](ABC*) {});
  abc.with_shared_lock([](const ABC*) {});

  abc.wait([]() { return true; });
  abc.wait([](ABC) { return true; });
  abc.wait([](ABC&) { return true; });
  abc.wait([](const ABC&) { return true; });
  abc.wait([](ABC*) { return true; });
  abc.wait([](const ABC*) { return true; });

  abc.shared_wait([]() { return true; });
  abc.shared_wait([](ABC) { return true; });
  // abc.shared_wait([](ABC&) { return true; });
  abc.shared_wait([](const ABC&) { return true; });
  // abc.shared_wait([](ABC*) { return true; });
  abc.shared_wait([](const ABC*) { return true; });

  abc.notify_one();
  abc.notify_one([]() {});
  abc.notify_one([](ABC) {});
  abc.notify_one([](ABC&) {});
  abc.notify_one([](const ABC&) {});
  abc.notify_one([](ABC*) {});
  abc.notify_one([](const ABC*) {});

  abc.shared_notify_one();
  abc.shared_notify_one([]() {});
  abc.shared_notify_one([](ABC) {});
  // abc.shared_notify_one([](ABC&) {});
  abc.shared_notify_one([](const ABC&) {});
  // abc.shared_notify_one([](ABC*) {});
  abc.shared_notify_one([](const ABC*) {});

  abc.notify_all();
  abc.notify_all([]() {});
  abc.notify_all([](ABC) {});
  abc.notify_all([](ABC&) {});
  abc.notify_all([](const ABC&) {});
  abc.notify_all([](ABC*) {});
  abc.notify_all([](const ABC*) {});

  abc.shared_notify_all();
  abc.shared_notify_all([]() {});
  abc.shared_notify_all([](ABC) {});
  // abc.shared_notify_all([](ABC&) {});
  abc.shared_notify_all([](const ABC&) {});
  // abc.shared_notify_all([](ABC*) {});
  abc.shared_notify_all([](const ABC*) {});

  const a0::sync<ABC> abc_const_sync;

  // abc_const_sync.with_lock([]() {});
  // abc_const_sync.with_lock([](ABC&) {});
  // abc_const_sync.with_lock([](const ABC&) {});
  // abc_const_sync.with_lock([](ABC*) {});
  // abc_const_sync.with_lock([](const ABC*) {});

  // abc_const_sync.with_shared_lock();
  abc_const_sync.with_shared_lock([]() {});
  abc_const_sync.with_shared_lock([](ABC) {});
  // abc_const_sync.with_shared_lock([](ABC&) {});
  abc_const_sync.with_shared_lock([](const ABC&) {});
  // abc_const_sync.with_shared_lock([](ABC*) {});
  abc_const_sync.with_shared_lock([](const ABC*) {});

  // abc_const_sync.wait();
  // abc_const_sync.wait([]() { return true; });
  // abc_const_sync.wait([](ABC) { return true; });
  // abc_const_sync.wait([](ABC&) { return true; });
  // abc_const_sync.wait([](const ABC&) { return true; });
  // abc_const_sync.wait([](ABC*) { return true; });
  // abc_const_sync.wait([](const ABC*) { return true; });

  abc_const_sync.shared_wait([]() { return true; });
  abc_const_sync.shared_wait([](ABC) { return true; });
  // abc_const_sync.shared_wait([](ABC&) { return true; });
  abc_const_sync.shared_wait([](const ABC&) { return true; });
  // abc_const_sync.shared_wait([](ABC*) { return true; });
  abc_const_sync.shared_wait([](const ABC*) { return true; });

  // abc_const_sync.notify_one();
  // abc_const_sync.notify_one([]() {});
  // abc_const_sync.notify_one([](ABC) {});
  // abc_const_sync.notify_one([](ABC&) {});
  // abc_const_sync.notify_one([](const ABC&) {});
  // abc_const_sync.notify_one([](ABC*) {});
  // abc_const_sync.notify_one([](const ABC*) {});

  abc_const_sync.shared_notify_one();
  abc_const_sync.shared_notify_one([]() {});
  abc_const_sync.shared_notify_one([](ABC) {});
  // abc_const_sync.shared_notify_one([](ABC&) {});
  abc_const_sync.shared_notify_one([](const ABC&) {});
  // abc_const_sync.shared_notify_one([](ABC*) {});
  abc_const_sync.shared_notify_one([](const ABC*) {});

  // abc_const_sync.notify_all();
  // abc_const_sync.notify_all([]() {});
  // abc_const_sync.notify_all([](ABC) {});
  // abc_const_sync.notify_all([](ABC&) {});
  // abc_const_sync.notify_all([](const ABC&) {});
  // abc_const_sync.notify_all([](ABC*) {});
  // abc_const_sync.notify_all([](const ABC*) {});

  abc_const_sync.shared_notify_all();
  abc_const_sync.shared_notify_all([]() {});
  abc_const_sync.shared_notify_all([](ABC) {});
  // abc_const_sync.shared_notify_all([](ABC&) {});
  abc_const_sync.shared_notify_all([](const ABC&) {});
  // abc_const_sync.shared_notify_all([](ABC*) {});
  abc_const_sync.shared_notify_all([](const ABC*) {});
}

namespace {

std::chrono::milliseconds short_dur() {
  if (a0::test::is_debug_mode()) {
    return std::chrono::milliseconds(10);
  }
  return std::chrono::milliseconds(1);
}

std::chrono::milliseconds long_dur() {
  if (a0::test::is_debug_mode()) {
    return std::chrono::milliseconds(100);
  }
  return std::chrono::milliseconds(10);
}

}  // namespace

TEST_CASE("sync] with_lock") {
  a0::sync<int> x{0};
  int y = 0;
  x.with_lock([&]() { y++; });
  x.with_lock([&](int x) { if (!x) y++; });
  x.with_lock([&](int& x) { x += 2; y++; });
  x.with_lock([&](const int& x) { if (x) y++; });
  x.with_lock([&](int* x) { *x += 2; y++; });
  x.with_lock([&](a0::monitor*) { y++; });
  x.with_lock([&](a0::monitor*, int x) { if (x) y++; });
  x.with_lock([&](a0::monitor*, int& x) { x += 2; y++; });
  x.with_lock([&](a0::monitor*, int* x) { *x += 2; y++; });
  x.with_shared_lock([&]() { y++; });
  x.with_shared_lock([&](int x) { if (x) y++; });
  x.with_shared_lock([&](const int& x) { if (x) y++; });
  x.with_shared_lock([&](const int* x) { if (*x) y++; });
  x.with_shared_lock([&](a0::monitor*) { y++; });
  x.with_shared_lock([&](a0::monitor*, int x) { if (x) y++; });
  x.with_shared_lock([&](a0::monitor*, const int& x) { if (x) y++; });
  x.with_shared_lock([&](a0::monitor*, const int* x) { if (*x) y++; });
  REQUIRE(x.copy() == 8);
  REQUIRE(y == 17);
}

TEST_CASE("sync] set copy") {
  a0::sync<int> x{1};
  REQUIRE(x.copy() == 1);
  x.set(2);
  REQUIRE(x.copy() == 2);
}

TEST_CASE("sync] notify one") {
  a0::sync<int> x{1};
  std::atomic<bool> y{false};
  std::thread t([&]() {
    x.wait([](int x_) { return x_ == 2; });
    x.notify_one([](int& x_) { x_ = 3; });
    x.shared_wait([](const int& x_) { return x_ == 4; });
    x.shared_notify_one([&]() { y = true; });
  });
  x.set(2);
  x.notify_one();
  x.wait([](int x_) { return x_ == 3; });
  REQUIRE(x.copy() == 3);
  x.with_lock([](int* x) { *x = 4; });
  x.shared_notify_one();
  x.wait([&]() { return bool(y); });
  t.join();
}

TEST_CASE("sync] notify all") {
  a0::sync<int> x{0};
  std::atomic<int> y{0};
  std::atomic<int> z{0};
  std::vector<std::thread> ts;

  for (int i = 0; i < 10; i++) {
    ts.emplace_back([&]() {
      x.notify_all([](int& x) { x++; });
      x.shared_wait([&]() { return y == 1; });
      x.notify_all([](int& x) { x++; });
      x.wait([&]() { return y == 2; });
      x.shared_notify_all([&]() { z++; });
    });
  }

  x.wait([](int x_) { return x_ == 10; });
  y = 1;
  x.notify_all();
  x.wait([](int x_) { return x_ == 20; });
  y = 2;
  x.shared_notify_all();
  x.wait([&]() { return z == 10; });
  for (auto&& t : ts) {
    t.join();
  }
}

TEST_CASE("sync] Event set") {
  a0::Event evt;
  REQUIRE(!evt.is_set());
  evt.set();
  REQUIRE(evt.is_set());
  evt.clear();
  REQUIRE(!evt.is_set());
}

TEST_CASE("sync] Event wait") {
  std::atomic<bool> set_by_thread = false;

  a0::Event evt;
  std::thread t{[&]() {
    set_by_thread = true;
    evt.set();
  }};
  evt.wait();

  REQUIRE(evt.is_set());
  REQUIRE(set_by_thread);

  t.join();
}

TEST_CASE("sync] Event wait_for no timeout") {
  std::atomic<bool> set_by_thread = false;

  a0::Event evt;
  std::thread t{[&]() {
    set_by_thread = true;
    evt.set();
  }};
  REQUIRE(evt.wait_for(long_dur()) == std::cv_status::no_timeout);

  REQUIRE(evt.is_set());
  REQUIRE(set_by_thread);

  t.join();
}

TEST_CASE("sync] Event wait_for timeout") {
  std::atomic<bool> set_by_thread = false;

  a0::Event evt;
  std::thread t{[&]() {
    std::this_thread::sleep_for(long_dur());
    set_by_thread = true;
    evt.set();
  }};
  REQUIRE(evt.wait_for(short_dur()) == std::cv_status::timeout);

  REQUIRE(!evt.is_set());
  REQUIRE(!set_by_thread);

  evt.wait();

  REQUIRE(evt.is_set());
  REQUIRE(set_by_thread);

  t.join();
}

TEST_CASE("sync] Event wait_until no timeout") {
  std::atomic<bool> set_by_thread = false;

  auto start = std::chrono::steady_clock::now();

  a0::Event evt;
  std::thread t{[&]() {
    set_by_thread = true;
    evt.set();
  }};
  REQUIRE(evt.wait_until(start + long_dur()) == std::cv_status::no_timeout);

  REQUIRE(evt.is_set());
  REQUIRE(set_by_thread);

  t.join();
}

TEST_CASE("sync] Event wait_until timeout") {
  std::atomic<bool> set_by_thread = false;

  auto start = std::chrono::steady_clock::now();

  a0::Event evt;
  std::thread t{[&]() {
    std::this_thread::sleep_for(long_dur());
    set_by_thread = true;
    evt.set();
  }};
  REQUIRE(evt.wait_until(start + short_dur()) == std::cv_status::timeout);

  REQUIRE(!evt.is_set());
  REQUIRE(!set_by_thread);

  evt.wait();

  REQUIRE(evt.is_set());
  REQUIRE(set_by_thread);

  t.join();
}
