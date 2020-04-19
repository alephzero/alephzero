#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <doctest.h>
#include <string.h>

#include <map>
#include <set>
#include <thread>
#include <vector>

#include "src/strutil.hpp"
#include "src/test_util.hpp"

TEST_CASE("topic_manager] basic") {
  a0_topic_alias_t subscriber_aliases[2] = {{
      .name = "ps0",
      .target_container = "ps0_container",
      .target_topic = "ps0_topic",
  }, {
      .name = "ps1",
      .target_container = "ps1_container",
      .target_topic = "ps1_topic",
  }};

  a0_topic_alias_t rpc_client_aliases[1] = {{
      .name = "rpc0",
      .target_container = "rpc0_container",
      .target_topic = "rpc0_topic",
  }};
  a0_topic_alias_t prpc_client_aliases[1] = {{
      .name = "prpc0",
      .target_container = "prpc0_container",
      .target_topic = "prpc0_topic",
  }};

  a0_topic_manager_t tm = {
      .container = "this_container",
      .subscriber_aliases = subscriber_aliases,
      .subscriber_aliases_size = 2,
      .rpc_client_aliases = rpc_client_aliases,
      .rpc_client_aliases_size = 1,
      .prpc_client_aliases = prpc_client_aliases,
      .prpc_client_aliases_size = 1,
  };

  auto REQUIRE_PATH = [&](std::function<errno_t(const a0_topic_manager_t*, a0_shm_t* out)> fn,
                          std::string path) {
    a0_shm_t shm;
    REQUIRE_OK(fn(&tm, &shm));
    REQUIRE(std::string(shm.path) == path);
    a0_shm_close(&shm);
    a0_shm_unlink(path.c_str());
  };

  auto REQUIRE_ALIAS_PATH =
      [&](std::function<errno_t(const a0_topic_manager_t*, const char*, a0_shm_t* out)> fn,
          const char* alias,
          std::string path) {
        a0_shm_t shm;
        REQUIRE_OK(fn(&tm, alias, &shm));
        REQUIRE(std::string(shm.path) == path);
        a0_shm_close(&shm);
        a0_shm_unlink(path.c_str());
      };

  REQUIRE_PATH(a0_topic_manager_open_config_topic, "/a0_config__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_crit_topic, "/a0_log_crit__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_err_topic, "/a0_log_err__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_warn_topic, "/a0_log_warn__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_info_topic, "/a0_log_info__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_dbg_topic, "/a0_log_dbg__this_container");

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_publisher_topic,
                     "ps0",
                     "/a0_pubsub__this_container__ps0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_subscriber_topic,
                     "ps0",
                     "/a0_pubsub__ps0_container__ps0_topic");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_subscriber_topic,
                     "ps1",
                     "/a0_pubsub__ps1_container__ps1_topic");

  {
    a0_shm_t shm;
    REQUIRE(a0_topic_manager_open_subscriber_topic(&tm, "ps2", &shm) == EINVAL);
  }

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_rpc_server_topic,
                     "rpc0",
                     "/a0_rpc__this_container__rpc0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_rpc_client_topic,
                     "rpc0",
                     "/a0_rpc__rpc0_container__rpc0_topic");

  {
    a0_shm_t shm;
    REQUIRE(a0_topic_manager_open_rpc_client_topic(&tm, "rpc1", &shm) == EINVAL);
  }

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_prpc_server_topic,
                     "prpc0",
                     "/a0_prpc__this_container__prpc0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_prpc_client_topic,
                     "prpc0",
                     "/a0_prpc__prpc0_container__prpc0_topic");

  {
    a0_shm_t shm;
    REQUIRE(a0_topic_manager_open_prpc_client_topic(&tm, "prpc1", &shm) == EINVAL);
  }
}