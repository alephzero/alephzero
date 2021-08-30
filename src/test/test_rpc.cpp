#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/rpc.h>
#include <a0/rpc.hpp>
#include <a0/string_view.hpp>
#include <a0/uuid.h>

#include <doctest.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "src/test_util.hpp"

struct RpcFixture {
  a0_rpc_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.rpc.a0";

  RpcFixture() {
    clear();
  }

  ~RpcFixture() {
    clear();
  }

  void clear() {
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(RpcFixture, "rpc] basic") {
  struct data_t {
    a0::test::Latch reply_latch{5};
    a0::test::Latch cancel_latch{5};
  } data{};

  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            if (a0::test::str(req.pkt.payload) == "reply") {
              REQUIRE_OK(a0_rpc_server_reply(req, a0::test::pkt("echo")));
            }
          },
  };

  a0_packet_id_callback_t oncancel = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_uuid_t) {
            auto* data = (data_t*)user_data;
            data->cancel_latch.count_down();
          },
  };

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, topic, a0::test::alloc(), onrequest, oncancel));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, topic, a0::test::alloc()));

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t) {
            auto* data = (data_t*)user_data;
            data->reply_latch.count_down();
          },
  };

  for (int i = 0; i < 5; i++) {
    REQUIRE_OK(a0_rpc_client_send(&client, a0::test::pkt("reply"), onreply));
  }

  for (int i = 0; i < 5; i++) {
    a0_packet_t req = a0::test::pkt("don't reply");
    REQUIRE_OK(a0_rpc_client_send(&client, req, onreply));
    REQUIRE_OK(a0_rpc_client_cancel(&client, req.id));
  }

  data.reply_latch.wait();
  data.cancel_latch.wait();

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp basic") {
  a0::test::Latch reply_latch{5};
  a0::test::Latch cancel_latch{5};

  auto onrequest = [&](a0::RpcRequest req) {
    if (req.pkt().payload() == "reply") {
      req.reply("echo");
    }
  };

  auto oncancel = [&](a0::string_view) {
    cancel_latch.count_down();
  };

  a0::RpcServer server("test", onrequest, oncancel);

  a0::RpcClient client("test");

  auto onreply = [&](a0::Packet) {
    reply_latch.count_down();
  };

  for (int i = 0; i < 5; i++) {
    client.send("reply", onreply);
  }

  for (int i = 0; i < 5; i++) {
    a0::Packet req("don't reply");
    client.send("don't reply", onreply);
    client.cancel(req.id());
  }

  reply_latch.wait();
  cancel_latch.wait();
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] empty oncancel onreply") {
  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            REQUIRE_OK(a0_rpc_server_reply(req, a0::test::pkt("echo")));
          },
  };

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, topic, a0::test::alloc(), onrequest, {}));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, topic, a0::test::alloc()));

  for (int i = 0; i < 5; i++) {
    auto req = a0::test::pkt("msg");
    REQUIRE_OK(a0_rpc_client_send(&client, req, {}));
    a0_rpc_client_cancel(&client, req.id);
  }

  // TODO: Find a better way.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}
