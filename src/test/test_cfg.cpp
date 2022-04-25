#include <a0/cfg.h>
#include <a0/cfg.hpp>
#include <a0/empty.h>
#include <a0/env.hpp>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "src/test_util.hpp"

#ifdef A0_EXT_NLOHMANN

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>

#endif  // A0_EXT_NLOHMANN

struct CfgFixture {
  a0_cfg_topic_t topic = {"test", nullptr};
  const char* topic_path = "test.cfg.a0";
  a0_cfg_t cfg = A0_EMPTY;

  CfgFixture() {
    clear();

    REQUIRE_OK(a0_cfg_init(&cfg, topic));
  }

  ~CfgFixture() {
    REQUIRE_OK(a0_cfg_close(&cfg));

    clear();
  }

  void clear() {
    setenv("A0_TOPIC", topic.name, true);
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(CfgFixture, "cfg] basic") {
  a0_packet_t pkt;
  REQUIRE(a0_cfg_read(&cfg, a0::test::alloc(), &pkt) == A0_ERR_AGAIN);

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("cfg")));
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");

  REQUIRE_OK(a0_cfg_read_blocking(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] write if empty") {
  bool written;

  REQUIRE_OK(a0_cfg_write_if_empty(&cfg, a0::test::pkt("cfg 0"), &written));
  REQUIRE(written);

  REQUIRE_OK(a0_cfg_write_if_empty(&cfg, a0::test::pkt("cfg 1"), &written));
  REQUIRE(!written);

  REQUIRE_OK(a0_cfg_write_if_empty(&cfg, a0::test::pkt("cfg 2"), nullptr));

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg 0");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp basic") {
  a0::Cfg c(a0::env::topic());

  REQUIRE_THROWS_WITH(
      c.read(),
      "Not available yet");

  c.write("cfg");
  REQUIRE(c.read_blocking().payload() == "cfg");
  REQUIRE(c.read().payload() == "cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] watcher") {
  struct data_t {
    std::vector<std::string> cfgs;
    a0_event_t got_final_cfg = A0_EMPTY;
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            data->cfgs.push_back(a0::test::str(pkt.payload));
            if (data->cfgs.back() == "final_cfg") {
              a0_event_set(&data->got_final_cfg);
            }
          },
  };

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("init_cfg")));

  a0_cfg_watcher_t watcher;
  REQUIRE_OK(a0_cfg_watcher_init(&watcher, topic, a0::test::alloc(), cb));

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("inter_cfg")));
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("final_cfg")));

  a0_event_wait(&data.got_final_cfg);
  REQUIRE_OK(a0_cfg_watcher_close(&watcher));

  REQUIRE(data.cfgs.size() >= 2);
  REQUIRE(data.cfgs.front() == "init_cfg");
  REQUIRE(data.cfgs.back() == "final_cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp watcher") {
  std::vector<std::string> cfgs;
  a0_event_t got_final_cfg = A0_EMPTY;

  a0::Cfg c(topic.name);
  c.write("init_cfg");

  a0::CfgWatcher watcher(topic.name, [&](a0::Packet pkt) {
    cfgs.push_back(std::string(pkt.payload()));
    if (cfgs.back() == "final_cfg") {
      a0_event_set(&got_final_cfg);
    }
  });

  c.write("inter_cfg");
  c.write("final_cfg");

  a0_event_wait(&got_final_cfg);

  REQUIRE(cfgs.size() >= 2);
  REQUIRE(cfgs.front() == "init_cfg");
  REQUIRE(cfgs.back() == "final_cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] mergepatch") {
  REQUIRE_OK(a0_cfg_mergepatch(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  REQUIRE_OK(a0_cfg_mergepatch(&cfg, a0::test::pkt(R"({"foo": null, "bar": {"baz": 3}})")));

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"({"bar":{"baz":3}})");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp mergepatch") {
  a0::Cfg c(topic.name);

  c.mergepatch(R"({"foo": 1,"bar": 2,"zzz":6})");
  REQUIRE(c.read().payload() == R"({"foo": 1,"bar": 2,"zzz":6})");

  c.mergepatch(std::string(R"({"foo": null, "bar": {"baz": 3, "bat": 4}})"));
  REQUIRE(c.read().payload() == R"({"bar":{"baz":3,"bat":4},"zzz":6})");

  c.mergepatch(a0::Packet(R"({"bar": {"bat": 5}})"));
  REQUIRE(c.read().payload() == R"({"bar":{"bat":5,"baz":3},"zzz":6})");

#ifdef A0_EXT_NLOHMANN
  c.mergepatch({{"zzz", nullptr}});
  REQUIRE(c.read().payload() == R"({"bar":{"bat":5,"baz":3}})");
#endif
}

#ifdef A0_EXT_NLOHMANN

struct MyStruct {
  int foo;
  int bar;
};

void from_json(const nlohmann::json& j, MyStruct& my) {
  j.at("foo").get_to(my.foo);
  j.at("bar").get_to(my.bar);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp nlohmann") {
  std::vector<nlohmann::json> saved_cfgs;
  std::mutex mu;
  std::condition_variable cv;

  auto block_until_saved = [&](size_t cnt) {
    std::unique_lock<std::mutex> lk(mu);
    cv.wait(lk, [&]() { return saved_cfgs.size() >= cnt; });
  };

  a0::CfgWatcher watcher(topic.name, [&](nlohmann::json j) {
    std::unique_lock<std::mutex> lk{mu};
    saved_cfgs.push_back(j);
    cv.notify_all();
  });

  a0::Cfg c(a0::env::topic());

  auto aaa = c.var<int>("/aaa");
  REQUIRE_THROWS_WITH(
      *aaa,
      "Cfg::Var(jptr=/aaa) has no data");

  REQUIRE(c.write_if_empty(R"({"foo": 1, "bar": 2})"));
  REQUIRE(!c.write_if_empty(R"({"foo": 1, "bar": 5})"));

  block_until_saved(1);

  a0::Cfg::Var<MyStruct> my;

  my = c.var<MyStruct>();
  auto foo = c.var<int>("/foo");

  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  c.write({{"foo", 3}, {"bar", 2}});
  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  c.update_var();
  REQUIRE(my->foo == 3);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 3);

  block_until_saved(2);

  c.mergepatch(nlohmann::json{{"foo", 4}});
  c.update_var();
  REQUIRE(my->foo == 4);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 4);

  block_until_saved(3);

  REQUIRE(saved_cfgs.size() == 3);
  REQUIRE(saved_cfgs[0] == nlohmann::json{{"bar", 2}, {"foo", 1}});
  REQUIRE(saved_cfgs[1] == nlohmann::json{{"bar", 2}, {"foo", 3}});
  REQUIRE(saved_cfgs[2] == nlohmann::json{{"bar", 2}, {"foo", 4}});
  watcher = {};

  c.write({{"aaa", 1}, {"bbb", 2}});
  c.update_var();
  REQUIRE_THROWS_WITH(
      *foo,
      "Cfg::Var(jptr=/foo) parse error: "
      "[json.exception.out_of_range.403] "
      "key 'foo' not found");

  c.write({{"aaa", 1}, {"foo", "notanumber"}});
  c.update_var();
  REQUIRE_THROWS_WITH(
      *foo,
      "Cfg::Var(jptr=/foo) parse error: "
      "[json.exception.type_error.302] "
      "type must be number, but is string");

  c.write("");
  REQUIRE_THROWS_WITH(
      c.update_var(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "unexpected end of input; expected '[', '{', or a literal");

  c.write("cfg");
  REQUIRE_THROWS_WITH(
      c.update_var(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "invalid literal; last read: 'c'");
}

#endif  // A0_EXT_NLOHMANN
