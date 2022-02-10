#include <a0/cfg.h>
#include <a0/cfg.hpp>
#include <a0/empty.h>
#include <a0/env.hpp>
#include <a0/err.h>
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

#ifdef A0_EXT_YYJSON

#include <a0/time.h>

#include <yyjson.h>

#include <cerrno>
#include <chrono>
#include <thread>

#include "src/err_macro.h"

#endif  // A0_EXT_YYJSON

#ifdef A0_EXT_NLOHMANN

#include <nlohmann/json.hpp>

#include <exception>
#include <memory>
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
      [&]() { c.read(); }(),
      "Not available yet");

  c.write("cfg");
  REQUIRE(c.read_blocking().payload() == "cfg");
  REQUIRE(c.read().payload() == "cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] watcher") {
  struct data_t {
    std::vector<std::string> cfgs;
    a0::test::Event got_final_cfg;
  } data{};

  a0_packet_callback_t cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (data_t*)user_data;
            data->cfgs.push_back(a0::test::str(pkt.payload));
            if (data->cfgs.back() == "final_cfg") {
              data->got_final_cfg.set();
            }
          },
  };

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("init_cfg")));

  a0_cfg_watcher_t watcher;
  REQUIRE_OK(a0_cfg_watcher_init(&watcher, topic, a0::test::alloc(), cb));

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("inter_cfg")));
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("final_cfg")));

  data.got_final_cfg.wait();
  REQUIRE_OK(a0_cfg_watcher_close(&watcher));

  REQUIRE(data.cfgs.size() >= 2);
  REQUIRE(data.cfgs.front() == "init_cfg");
  REQUIRE(data.cfgs.back() == "final_cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp watcher") {
  std::vector<std::string> cfgs;
  a0::test::Event got_final_cfg;

  a0::Cfg c(topic.name);
  c.write("init_cfg");

  a0::CfgWatcher watcher(topic.name, [&](a0::Packet pkt) {
    cfgs.push_back(std::string(pkt.payload()));
    if (cfgs.back() == "final_cfg") {
      got_final_cfg.set();
    }
  });

  c.write("inter_cfg");
  c.write("final_cfg");

  got_final_cfg.wait();

  REQUIRE(cfgs.size() >= 2);
  REQUIRE(cfgs.front() == "init_cfg");
  REQUIRE(cfgs.back() == "final_cfg");
}

#ifdef A0_EXT_YYJSON

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read empty nonblock") {
  yyjson_doc doc;
  REQUIRE(a0_cfg_read_yyjson(&cfg, a0::test::alloc(), &doc) == A0_ERR_AGAIN);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read nonjson") {
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("cfg")));
  yyjson_doc doc;
  a0_err_t err = a0_cfg_read_yyjson(&cfg, a0::test::alloc(), &doc);
  REQUIRE(err == A0_ERR_CUSTOM_MSG);
  REQUIRE(std::string(a0_err_msg) == "Failed to parse cfg: unexpected character");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read valid") {
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  yyjson_doc doc;
  REQUIRE_OK(a0_cfg_read_yyjson(&cfg, a0::test::alloc(), &doc));
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "foo")) == 1);
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "bar")) == 2);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read blocking") {
  std::thread t([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  });
  yyjson_doc doc;
  REQUIRE_OK(a0_cfg_read_blocking_yyjson(&cfg, a0::test::alloc(), &doc));
  t.join();

  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "foo")) == 1);
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "bar")) == 2);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read blocking timeout success") {
  std::thread t([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  });
  a0_time_mono_t timeout = a0::test::timeout_in(std::chrono::milliseconds(25));

  yyjson_doc doc;
  REQUIRE_OK(a0_cfg_read_blocking_timeout_yyjson(&cfg, a0::test::alloc(), &timeout, &doc));
  t.join();

  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "foo")) == 1);
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "bar")) == 2);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read blocking timeout fail") {
  std::thread t([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  });
  a0_time_mono_t timeout = a0::test::timeout_in(std::chrono::milliseconds(1));

  yyjson_doc doc;
  REQUIRE(A0_SYSERR(a0_cfg_read_blocking_timeout_yyjson(&cfg, a0::test::alloc(), &timeout, &doc)) == ETIMEDOUT);
  t.join();
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson write") {
  std::string json_str = R"([1, "2", "three"])";
  yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
  REQUIRE_OK(a0_cfg_write_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"([1,"2","three"])");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson write if empty") {
  bool written;
  std::string json_str = R"([1, "2", "three"])";
  yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
  REQUIRE_OK(a0_cfg_write_if_empty_yyjson(&cfg, *doc, &written));
  yyjson_doc_free(doc);

  json_str = R"([1, "2", "three", "four"])";
  doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
  REQUIRE_OK(a0_cfg_write_if_empty_yyjson(&cfg, *doc, &written));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"([1,"2","three"])");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson mergepatch") {
  std::string mp_str = R"({"foo": 1,"bar": 2})";
  yyjson_doc* doc = yyjson_read(mp_str.c_str(), mp_str.size(), 0);
  REQUIRE_OK(a0_cfg_mergepatch_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  mp_str = R"({"foo": null, "bar": {"baz": 3}})";
  doc = yyjson_read(mp_str.c_str(), mp_str.size(), 0);
  REQUIRE_OK(a0_cfg_mergepatch_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"({"bar":{"baz":3}})");
}

#endif  // A0_EXT_YYJSON

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
  std::unique_ptr<a0::test::Latch> latch;

  a0::CfgWatcher watcher(topic.name, [&](nlohmann::json j) {
    saved_cfgs.push_back(j);
    latch->count_down();
  });

  latch.reset(new a0::test::Latch(2));
  a0::Cfg c(a0::env::topic());

  auto aaa = c.var<int>("/aaa");
  REQUIRE_THROWS_WITH(
      [&]() { *aaa; }(),
      "Cfg::Var(jptr=/aaa) has no data");

  REQUIRE(c.write_if_empty(R"({"foo": 1, "bar": 2})"));
  REQUIRE(!c.write_if_empty(R"({"foo": 1, "bar": 5})"));
  latch->arrive_and_wait();

  a0::Cfg::Var<MyStruct> my;

  my = c.var<MyStruct>();
  auto foo = c.var<int>("/foo");

  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  latch.reset(new a0::test::Latch(2));
  c.write({{"foo", 3}, {"bar", 2}});
  REQUIRE(my->foo == 1);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 1);

  c.update_var();
  REQUIRE(my->foo == 3);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 3);
  latch->arrive_and_wait();

  latch.reset(new a0::test::Latch(2));
  c.mergepatch({{"foo", 4}});
  c.update_var();
  REQUIRE(my->foo == 4);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 4);
  latch->arrive_and_wait();

  REQUIRE(saved_cfgs.size() == 3);
  REQUIRE(saved_cfgs[0] == nlohmann::json{{"bar", 2}, {"foo", 1}});
  REQUIRE(saved_cfgs[1] == nlohmann::json{{"bar", 2}, {"foo", 3}});
  REQUIRE(saved_cfgs[2] == nlohmann::json{{"bar", 2}, {"foo", 4}});
  watcher = {};

  c.write({{"aaa", 1}, {"bbb", 2}});
  c.update_var();
  REQUIRE_THROWS_WITH(
      [&]() { *foo; }(),
      "Cfg::Var(jptr=/foo) parse error: "
      "[json.exception.out_of_range.403] "
      "key 'foo' not found");

  c.write({{"aaa", 1}, {"foo", "notanumber"}});
  c.update_var();
  REQUIRE_THROWS_WITH(
      [&]() { *foo; }(),
      "Cfg::Var(jptr=/foo) parse error: "
      "[json.exception.type_error.302] "
      "type must be number, but is string");

  c.write("");
  REQUIRE_THROWS_WITH(
      [&]() { c.update_var(); }(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "unexpected end of input; expected '[', '{', or a literal");

  c.write("cfg");
  REQUIRE_THROWS_WITH(
      [&]() { c.update_var(); }(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "invalid literal; last read: 'c'");
}

#endif  // A0_EXT_NLOHMANN
