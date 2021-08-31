#include <a0/config.h>
#include <a0/config.hpp>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>
#include <fcntl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "src/test_util.hpp"

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

#include <nlohmann/json.hpp>

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

struct ConfigFixture {
  a0_config_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.cfg.a0";

  ConfigFixture() {
    clear();
  }

  ~ConfigFixture() {
    clear();
  }

  void clear() {
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(ConfigFixture, "config] basic") {
  a0_packet_t cfg;
  REQUIRE(a0_config(topic, a0::test::alloc(), O_NONBLOCK, &cfg) == EAGAIN);

  REQUIRE_OK(a0_write_config(topic, a0::test::pkt("cfg")));
  REQUIRE_OK(a0_config(topic, a0::test::alloc(), O_NONBLOCK, &cfg));
  REQUIRE(a0::test::str(cfg.payload) == "cfg");

  REQUIRE_OK(a0_config(topic, a0::test::alloc(), 0, &cfg));
  REQUIRE(a0::test::str(cfg.payload) == "cfg");
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp basic") {
  REQUIRE_THROWS_WITH(
      [&]() { a0::config("test", O_NONBLOCK); }(),
      "Resource temporarily unavailable");

  a0::write_config("test", "cfg");
  REQUIRE(a0::config("test").payload() == "cfg");
  REQUIRE(a0::config("test", O_NONBLOCK).payload() == "cfg");
}

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

struct MyStruct {
  int foo;
  int bar;
};

void from_json(const nlohmann::json& j, MyStruct& my) {
  j["foo"].get_to(my.foo);
  j["bar"].get_to(my.bar);
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp nlohmann") {
  a0::write_config("test", R"({"foo": 1, "bar": 2})");

  a0::cfg<MyStruct> my("test", "");
  a0::cfg<int> foo("test", "/foo");

  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  a0::write_config("test", R"({"foo": 3, "bar": 2})");
  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  a0::update_configs();
  REQUIRE(my->foo == 3);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 3);

  a0::write_config("test", "cfg");
  a0::update_configs();

  REQUIRE_THROWS_WITH(
      [&]() { my->foo; }(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "invalid literal; last read: 'c'");

  REQUIRE_THROWS_WITH(
      [&]() { *foo; }(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "invalid literal; last read: 'c'");
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp nlohmann split threads") {
  a0::write_config("test", R"({"foo": 1, "bar": 2})");
  a0::cfg<int> x("test", "/foo");

  std::vector<std::thread> threads;

  std::vector<std::unique_ptr<a0::test::Latch>> latches;
  for (int i = 0; i < 6; i++) {
    latches.push_back(std::unique_ptr<a0::test::Latch>(new a0::test::Latch(2)));
  }

  threads.emplace_back([&]() {
    REQUIRE(*x == 1);
    latches[0]->arrive_and_wait();
    a0::write_config("test", R"({"foo": 3})");
    latches[1]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[2]->arrive_and_wait();
    a0::update_configs();
    latches[3]->arrive_and_wait();                                                                
    REQUIRE(*x == 3);
    latches[4]->arrive_and_wait();
    latches[5]->arrive_and_wait();
  });

  threads.emplace_back([&]() {
    REQUIRE(*x == 1);
    latches[0]->arrive_and_wait();
    // other thread write_config
    latches[1]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[2]->arrive_and_wait();
    // other thread update_configs
    latches[3]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[4]->arrive_and_wait();
    a0::update_configs();
    latches[5]->arrive_and_wait();
    REQUIRE(*x == 3);
  });

  for (auto&& t : threads) {
    t.join();
  }
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp nlohmann per threads") {
  a0::write_config("test", R"({"foo": 1, "bar": 2})");

  std::vector<std::thread> threads;

  std::vector<std::unique_ptr<a0::test::Latch>> latches;
  for (int i = 0; i < 6; i++) {
    latches.push_back(std::unique_ptr<a0::test::Latch>(new a0::test::Latch(2)));
  }

  threads.emplace_back([&]() {
    a0::cfg<int> x("test", "/foo");

    REQUIRE(*x == 1);
    latches[0]->arrive_and_wait();
    a0::write_config("test", R"({"foo": 3})");
    latches[1]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[2]->arrive_and_wait();
    a0::update_configs();
    latches[3]->arrive_and_wait();
    REQUIRE(*x == 3);
    latches[4]->arrive_and_wait();
    latches[5]->arrive_and_wait();
  });

  threads.emplace_back([&]() {
    a0::cfg<int> x("test", "/foo");

    REQUIRE(*x == 1);
    latches[0]->arrive_and_wait();
    // other thread write_config
    latches[1]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[2]->arrive_and_wait();
    // other thread update_configs
    latches[3]->arrive_and_wait();
    REQUIRE(*x == 1);
    latches[4]->arrive_and_wait();
    a0::update_configs();
    latches[5]->arrive_and_wait();
    REQUIRE(*x == 3);
  });

  for (auto&& t : threads) {
    t.join();
  }
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp nlohmann fuzz") {
  a0::write_config("test", R"({"foo": 1})");

  a0::cfg<int> x("test", "/foo");
  int real_x = *x;
  std::mutex mu;

  std::atomic<bool> done{false};

  std::vector<std::function<void(int*)>> actions = {
      [&](int* local_x) {
        a0::update_configs();

        // Update expected local value.
        std::unique_lock<std::mutex> lk{mu};
        REQUIRE(*x == real_x);
        *local_x = real_x;
      },
      [&](int*) {
        // Change global/external value.
        std::unique_lock<std::mutex> lk{mu};
        real_x = rand();
        a0::write_config("test", nlohmann::json{ {"foo", real_x} }.dump());
      },
      [&](int* local_x) { REQUIRE(*x == *local_x); },
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&]() {
      int local_x;
      // Initial update_configs.
      actions[0](&local_x);

      while (!done) {
        auto action = actions[rand() % actions.size()];
        action(&local_x);
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  done = true;
  for (auto&& t : threads) {
    t.join();
  }
}

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
