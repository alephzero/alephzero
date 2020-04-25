#include <doctest.h>

#include <map>
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

  abc.wait();
  abc.wait([]() { return true; });
  abc.wait([](ABC) { return true; });
  abc.wait([](ABC&) { return true; });
  abc.wait([](const ABC&) { return true; });
  abc.wait([](ABC*) { return true; });
  abc.wait([](const ABC*) { return true; });

  abc.shared_wait();
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

  abc_const_sync.shared_wait();
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

TEST_CASE("sync] set copy") {
  a0::sync<int> x{1};
  REQUIRE(x.copy() == 1);
  x.set(2);
  REQUIRE(x.copy() == 2);
}

TEST_CASE("sync] wait notify") {
  a0::sync<int> x{1};
  std::thread t([&]() {
    x.wait([](int x_) { return x_ == 2; });
    x.notify_all([](int& x_) { x_ = 3; });
  });
  x.notify_all([](int& x_) { x_ = 2; });
  x.wait([](int x_) { return x_ == 3; });
  REQUIRE(x.copy() == 3);
  t.join();
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
  bool set_by_thread = false;

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
