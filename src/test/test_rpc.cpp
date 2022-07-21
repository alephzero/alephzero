#include <a0/file.h>
#include <a0/latch.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/rpc.h>
#include <a0/rpc.hpp>
#include <a0/string_view.hpp>
#include <a0/time.hpp>
#include <a0/uuid.h>

#include <doctest.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <ostream>
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

// TEST_CASE_FIXTURE(RpcFixture, "rpc] basic") {
//   struct data_t {
//     a0_latch_t reply_latch;
//     a0_latch_t cancel_latch;
//   } data{};
//   a0_latch_init(&data.reply_latch, 5);
//   a0_latch_init(&data.cancel_latch, 5);

//   a0_rpc_request_callback_t onrequest = {
//       .user_data = nullptr,
//       .fn =
//           [](void*, a0_rpc_request_t req) {
//             if (a0::test::str(req.pkt.payload) == "reply") {
//               REQUIRE_OK(a0_rpc_server_reply(req, a0::test::pkt("echo")));
//             }
//           },
//   };

//   a0_packet_id_callback_t oncancel = {
//       .user_data = &data,
//       .fn =
//           [](void* user_data, a0_uuid_t) {
//             auto* data = (data_t*)user_data;
//             a0_latch_count_down(&data->cancel_latch, 1);
//           },
//   };

//   a0_rpc_server_t server;
//   REQUIRE_OK(a0_rpc_server_init(&server, topic, a0::test::alloc(), {onrequest, oncancel, A0_TIMEOUT_NEVER}));

//   a0_rpc_client_t client;
//   REQUIRE_OK(a0_rpc_client_init(&client, topic, a0::test::alloc()));

//   a0_packet_callback_t onreply = {
//       .user_data = &data,
//       .fn =
//           [](void* user_data, a0_packet_t) {
//             auto* data = (data_t*)user_data;
//             a0_latch_count_down(&data->reply_latch, 1);
//           },
//   };

//   for (int i = 0; i < 5; i++) {
//     REQUIRE_OK(a0_rpc_client_send(&client, a0::test::pkt("reply"), onreply));
//   }

//   for (int i = 0; i < 5; i++) {
//     a0_packet_t req = a0::test::pkt("don't reply");
//     REQUIRE_OK(a0_rpc_client_send(&client, req, onreply));
//     REQUIRE_OK(a0_rpc_client_cancel(&client, req.id));
//   }

//   a0_latch_wait(&data.reply_latch);
//   a0_latch_wait(&data.cancel_latch);

//   REQUIRE_OK(a0_rpc_client_close(&client));
//   REQUIRE_OK(a0_rpc_server_close(&server));
// }

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp basic") {
  a0_latch_t reply_latch;
  a0_latch_init(&reply_latch, 5);

  a0::RpcServer server("test", [&](a0::RpcRequest req) {
    req.reply("echo");
  });

  a0::RpcClient client("test");

  for (int i = 0; i < 5; i++) {
    client.send("request", [&](a0::Packet) {
      a0_latch_count_down(&reply_latch, 1);
    });
  }

  a0_latch_wait(&reply_latch);
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp cancel") {
  a0_latch_t reply_latch;
  a0_latch_init(&reply_latch, 5);
  a0_latch_t cancel_latch;
  a0_latch_init(&cancel_latch, 5);

  auto onrequest = [&](a0::RpcRequest req) {
    if (req.pkt().payload() == "reply") {
      req.reply("echo");
    }
  };

  auto oncancel = [&](a0::string_view) {
    a0_latch_count_down(&cancel_latch, 1);
  };

  a0::RpcServer server("test", onrequest, oncancel);

  a0::RpcClient client("test");

  auto onreply = [&](a0::Packet) {
    a0_latch_count_down(&reply_latch, 1);
  };

  for (int i = 0; i < 5; i++) {
    client.send("reply", onreply);
  }

  for (int i = 0; i < 5; i++) {
    a0::Packet req("don't reply");
    client.send("don't reply", onreply);
    client.cancel(req.id());
  }

  a0_latch_wait(&reply_latch);
  a0_latch_wait(&cancel_latch);
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp server restart") {
  a0_latch_t reply_latch;
  a0_latch_init(&reply_latch, 5);

  std::unique_ptr<a0::RpcServer> server;
  server.reset(new a0::RpcServer("test", [&](a0::RpcRequest) {
    /* no-op */
  }));

  a0::RpcClient client("test");

  for (int i = 0; i < 5; i++) {
    client.send("request", [&](a0::Packet) {
      a0_latch_count_down(&reply_latch, 1);
    });
  }

  // Cannot have two servers on the same topic at the same time.
  // Release before creating a new one.
  server.reset();
  server.reset(new a0::RpcServer("test", [&](a0::RpcRequest req) {
    req.reply("echo");
  }));

  a0_latch_wait(&reply_latch);
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp blocking") {
  a0::RpcServer server("test", [&](a0::RpcRequest req) {
    req.reply("echo");
  });

  a0::RpcClient client("test");

  for (int i = 0; i < 5; i++) {
    auto reply = client.send_blocking("request");
    REQUIRE(reply.payload() == "echo");
  }
}

// TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp blocking") {
//   a0::RpcServer server(
//       "test", [](a0::RpcRequest req) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(10));
//         req.reply("reply");
//       },
//       nullptr);

//   REQUIRE(a0::RpcClient("test").send_blocking("send").payload() == "reply");

//   auto timeout = a0::TimeMono::now() + std::chrono::milliseconds(a0::test::is_valgrind() ? 200 : 20);
//   REQUIRE(a0::RpcClient("test").send_blocking("send", timeout).payload() == "reply");

//   timeout = a0::TimeMono::now() + std::chrono::milliseconds(1);
//   REQUIRE_THROWS_WITH(
//       a0::RpcClient("test").send_blocking("send", timeout),
//       strerror(ETIMEDOUT));

//   REQUIRE(a0::RpcClient("test").send("send").get().payload() == "reply");

//   server = {};

//   a0::Packet pkt_0("send");
//   std::thread t_0([&]() {
//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     a0::RpcClient("test").cancel(pkt_0.id());
//   });
//   REQUIRE_THROWS_WITH(
//       a0::RpcClient("test").send_blocking(pkt_0),
//       "Operation cancelled");
//   t_0.join();

//   a0::Packet pkt_1("send");
//   std::thread t_1([&]() {
//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     a0::RpcClient("test").cancel(pkt_1.id());
//   });
//   timeout = a0::TimeMono::now() + std::chrono::seconds(2000);
//   REQUIRE_THROWS_WITH(
//       a0::RpcClient("test").send_blocking(pkt_1, timeout),
//       "Operation cancelled");
//   t_1.join();
// }

// TEST_CASE_FIXTURE(RpcFixture, "rpc] empty oncancel onreply") {
//   a0_rpc_request_callback_t onrequest = {
//       .user_data = nullptr,
//       .fn =
//           [](void*, a0_rpc_request_t req) {
//             REQUIRE_OK(a0_rpc_server_reply(req, a0::test::pkt("echo")));
//           },
//   };

//   a0_rpc_server_t server;
//   REQUIRE_OK(a0_rpc_server_init(&server, topic, a0::test::alloc(), {onrequest, {}, A0_TIMEOUT_NEVER}));

//   a0_rpc_client_t client;
//   REQUIRE_OK(a0_rpc_client_init(&client, topic, a0::test::alloc()));

//   for (int i = 0; i < 5; i++) {
//     auto req = a0::test::pkt("msg");
//     REQUIRE_OK(a0_rpc_client_send(&client, req, {}));
//     a0_rpc_client_cancel(&client, req.id);
//   }

//   // TODO: Find a better way.
//   std::this_thread::sleep_for(std::chrono::milliseconds(1));

//   REQUIRE_OK(a0_rpc_client_close(&client));
//   REQUIRE_OK(a0_rpc_server_close(&server));
// }
