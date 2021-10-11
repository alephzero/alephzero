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
#include <fcntl.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "src/test_util.hpp"

#ifdef A0_EXT_YYJSON

#include <yyjson.h>

#endif  // A0_EXT_YYJSON

#ifdef A0_EXT_NLOHMANN

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <vector>

#endif  // A0_EXT_NLOHMANN

struct CfgFixture {
  a0_cfg_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.cfg.a0";
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
  REQUIRE(a0_cfg_read(&cfg, a0::test::alloc(), O_NONBLOCK, &pkt) == A0_ERR_AGAIN);

  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("cfg")));
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), O_NONBLOCK, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");

  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), 0, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp basic") {
  a0::Cfg c(a0::env::topic());

  REQUIRE_THROWS_WITH(
      [&]() { c.read(O_NONBLOCK); }(),
      "Not available yet");

  c.write("cfg");
  REQUIRE(c.read().payload() == "cfg");
  REQUIRE(c.read(O_NONBLOCK).payload() == "cfg");
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
  REQUIRE(a0_cfg_read_yyjson(&cfg, a0::test::alloc(), O_NONBLOCK, &doc) == A0_ERR_AGAIN);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read nonjson") {
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt("cfg")));
  yyjson_doc doc;
  a0_err_t err = a0_cfg_read_yyjson(&cfg, a0::test::alloc(), 0, &doc);
  REQUIRE(err == A0_ERR_CUSTOM_MSG);
  REQUIRE(std::string(a0_err_msg) == "Failed to parse cfg: unexpected character");
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson read valid") {
  REQUIRE_OK(a0_cfg_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  yyjson_doc doc;
  REQUIRE_OK(a0_cfg_read_yyjson(&cfg, a0::test::alloc(), 0, &doc));
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "foo")) == 1);
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "bar")) == 2);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] yyjson write") {
  std::string json_str = R"([1, "2", "three"])";
  yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
  REQUIRE_OK(a0_cfg_write_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), 0, &pkt));
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
  REQUIRE_OK(a0_cfg_read(&cfg, a0::test::alloc(), 0, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"({"bar":{"baz":3}})");
}

#endif

#ifdef A0_EXT_NLOHMANN

struct MyStruct {
  int foo;
  int bar;
};

void from_json(const nlohmann::json& j, MyStruct& my) {
  j["foo"].get_to(my.foo);
  j["bar"].get_to(my.bar);
}

TEST_CASE_FIXTURE(CfgFixture, "cfg] cpp nlohmann") {
  a0::Cfg c(a0::env::topic());
  c.write(R"({"foo": 1, "bar": 2})");

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

  c.mergepatch({{"foo", 4}});
  c.update_var();
  REQUIRE(my->foo == 4);
  REQUIRE(my->bar == 2);
  REQUIRE(*foo == 4);

  c.write("cfg");
  REQUIRE_THROWS_WITH(
      [&]() { c.update_var(); }(),
      "[json.exception.parse_error.101] "
      "parse error at line 1, column 1: "
      "syntax error while parsing value - "
      "invalid literal; last read: 'c'");
}

#endif  // A0_EXT_NLOHMANN
