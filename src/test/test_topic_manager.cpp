#include <a0/arena.h>
#include <a0/errno.h>
#include <a0/topic_manager.h>

#include <doctest.h>

#include <cerrno>
#include <functional>
#include <string>

#include "src/test_util.hpp"

TEST_CASE("topic_manager] basic") {
  a0_topic_alias_t subscriber_aliases[2] = {
      {
          .name = "ps0",
          .target_container = "ps0_container",
          .target_topic = "ps0_topic",
      },
      {
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

  auto REQUIRE_PATH = [&](std::function<errno_t(const a0_topic_manager_t*, a0_file_t* out)> fn,
                          std::string path) {
    a0_file_t file;
    REQUIRE_OK(fn(&tm, &file));
    REQUIRE(std::string(file.path) == path);
    a0_file_close(&file);
    a0_file_remove(path.c_str());
  };

  auto REQUIRE_ALIAS_PATH =
      [&](std::function<errno_t(const a0_topic_manager_t*, const char*, a0_file_t* out)> fn,
          const char* alias,
          std::string path) {
        a0_file_t file;
        REQUIRE_OK(fn(&tm, alias, &file));
        REQUIRE(std::string(file.path) == path);
        a0_file_close(&file);
        a0_file_remove(path.c_str());
      };

  REQUIRE_PATH(a0_topic_manager_open_config_topic, "/dev/shm/a0_config__this_container");
  REQUIRE_PATH(a0_topic_manager_open_heartbeat_topic, "/dev/shm/a0_heartbeat__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_crit_topic, "/dev/shm/a0_log_crit__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_err_topic, "/dev/shm/a0_log_err__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_warn_topic, "/dev/shm/a0_log_warn__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_info_topic, "/dev/shm/a0_log_info__this_container");
  REQUIRE_PATH(a0_topic_manager_open_log_dbg_topic, "/dev/shm/a0_log_dbg__this_container");

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_publisher_topic,
                     "ps0",
                     "/dev/shm/a0_pubsub__this_container__ps0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_subscriber_topic,
                     "ps0",
                     "/dev/shm/a0_pubsub__ps0_container__ps0_topic");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_subscriber_topic,
                     "ps1",
                     "/dev/shm/a0_pubsub__ps1_container__ps1_topic");

  {
    a0_file_t file;
    REQUIRE(a0_topic_manager_open_subscriber_topic(&tm, "ps2", &file) == EINVAL);
  }

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_rpc_server_topic,
                     "rpc0",
                     "/dev/shm/a0_rpc__this_container__rpc0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_rpc_client_topic,
                     "rpc0",
                     "/dev/shm/a0_rpc__rpc0_container__rpc0_topic");

  {
    a0_file_t file;
    REQUIRE(a0_topic_manager_open_rpc_client_topic(&tm, "rpc1", &file) == EINVAL);
  }

  REQUIRE_ALIAS_PATH(a0_topic_manager_open_prpc_server_topic,
                     "prpc0",
                     "/dev/shm/a0_prpc__this_container__prpc0");
  REQUIRE_ALIAS_PATH(a0_topic_manager_open_prpc_client_topic,
                     "prpc0",
                     "/dev/shm/a0_prpc__prpc0_container__prpc0_topic");

  {
    a0_file_t file;
    REQUIRE(a0_topic_manager_open_prpc_client_topic(&tm, "prpc1", &file) == EINVAL);
  }
}
