#include <a0/config.h>
#include <a0/config.hpp>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>
#include <fcntl.h>

#include <cstdlib>
#include <ostream>
#include <string>

#include "src/test_util.hpp"

#ifdef A0_C_CONFIG_USE_YYJSON

#include <yyjson.h>

#endif  // A0_C_CONFIG_USE_YYJSON

#ifdef A0_CXX_CONFIG_USE_NLOHMANN

#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#endif  // A0_CXX_CONFIG_USE_NLOHMANN

struct ConfigFixture {
  a0_config_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.cfg.a0";
  a0_config_t cfg = A0_EMPTY;

  ConfigFixture() {
    clear();

    REQUIRE_OK(a0_config_init(&cfg, topic));
  }

  ~ConfigFixture() {
    REQUIRE_OK(a0_config_close(&cfg));

    clear();
  }

  void clear() {
    setenv("A0_NODE", topic.name, true);
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(ConfigFixture, "config] basic") {
  a0_packet_t pkt;
  REQUIRE(a0_config_read(&cfg, a0::test::alloc(), O_NONBLOCK, &pkt) == A0_ERR_AGAIN);

  REQUIRE_OK(a0_config_write(&cfg, a0::test::pkt("cfg")));
  REQUIRE_OK(a0_config_read(&cfg, a0::test::alloc(), O_NONBLOCK, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");

  REQUIRE_OK(a0_config_read(&cfg, a0::test::alloc(), 0, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == "cfg");
}

TEST_CASE_FIXTURE(ConfigFixture, "config] cpp basic") {
  a0::Config c(topic.name);

  REQUIRE_THROWS_WITH(
      [&]() { c.read(O_NONBLOCK); }(),
      "Not available yet");

  c.write("cfg");
  REQUIRE(c.read().payload() == "cfg");
  REQUIRE(c.read(O_NONBLOCK).payload() == "cfg");
}

#ifdef A0_C_CONFIG_USE_YYJSON

TEST_CASE_FIXTURE(ConfigFixture, "config] yyjson read empty nonblock") {
  yyjson_doc doc;
  REQUIRE(a0_config_read_yyjson(&cfg, a0::test::alloc(), O_NONBLOCK, &doc) == A0_ERR_AGAIN);
}

TEST_CASE_FIXTURE(ConfigFixture, "config] yyjson read nonjson") {
  REQUIRE_OK(a0_config_write(&cfg, a0::test::pkt("cfg")));
  yyjson_doc doc;
  a0_err_t err = a0_config_read_yyjson(&cfg, a0::test::alloc(), 0, &doc);
  REQUIRE(err == A0_ERR_CUSTOM_MSG);
  REQUIRE(std::string(a0_err_msg) == "Failed to parse config: unexpected character");
}

TEST_CASE_FIXTURE(ConfigFixture, "config] yyjson read valid") {
  REQUIRE_OK(a0_config_write(&cfg, a0::test::pkt(R"({"foo": 1,"bar": 2})")));
  yyjson_doc doc;
  REQUIRE_OK(a0_config_read_yyjson(&cfg, a0::test::alloc(), 0, &doc));
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "foo")) == 1);
  REQUIRE(yyjson_get_int(yyjson_obj_get(doc.root, "bar")) == 2);
}

TEST_CASE_FIXTURE(ConfigFixture, "config] yyjson write") {
  std::string json_str = R"([1, "2", "three"])";
  yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
  REQUIRE_OK(a0_config_write_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_config_read(&cfg, a0::test::alloc(), 0, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"([1,"2","three"])");
}

TEST_CASE_FIXTURE(ConfigFixture, "config] yyjson mergepatch") {
  std::string mp_str = R"({"foo": 1,"bar": 2})";
  yyjson_doc* doc = yyjson_read(mp_str.c_str(), mp_str.size(), 0);
  REQUIRE_OK(a0_config_mergepatch_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  mp_str = R"({"foo": null, "bar": {"baz": 3}})";
  doc = yyjson_read(mp_str.c_str(), mp_str.size(), 0);
  REQUIRE_OK(a0_config_mergepatch_yyjson(&cfg, *doc));
  yyjson_doc_free(doc);

  a0_packet_t pkt;
  REQUIRE_OK(a0_config_read(&cfg, a0::test::alloc(), 0, &pkt));
  REQUIRE(a0::test::str(pkt.payload) == R"({"bar":{"baz":3}})");
}

#endif

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
  a0::Config c;
  c.write(R"({"foo": 1, "bar": 2})");

  a0::CfgVar<MyStruct> my;

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

#endif  // A0_CXX_CONFIG_USE_NLOHMANN
