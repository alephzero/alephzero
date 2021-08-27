#include <a0/file.h>
#include <a0/config.h>
#include <a0/packet.h>

#include <doctest.h>
#include <stddef.h>
#include <fcntl.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "src/test_util.hpp"

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
