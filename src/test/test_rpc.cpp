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

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp basic") {
  a0_latch_t latch;
  a0_latch_init(&latch, 5);

  a0::RpcServer server("test", [&](a0::RpcRequest req) {
    req.reply("echo");
  });

  a0::RpcClient client("test");

  for (int i = 0; i < 5; i++) {
    client.send("request", [&](a0::Packet) {
      a0_latch_count_down(&latch, 1);
    });
  }

  a0_latch_wait(&latch);
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
  a0_latch_t latch;
  a0_latch_init(&latch, 5);

  std::unique_ptr<a0::RpcServer> server;
  server.reset(new a0::RpcServer("test", [&](a0::RpcRequest) {
    /* no-op */
  }));

  a0::RpcClient client("test");

  for (int i = 0; i < 5; i++) {
    client.send("request", [&](a0::Packet) {
      a0_latch_count_down(&latch, 1);
    });
  }

  // Cannot have two servers on the same topic at the same time.
  // Release before creating a new one.
  server.reset();
  server.reset(new a0::RpcServer("test", [&](a0::RpcRequest req) {
    req.reply("echo");
  }));

  a0_latch_wait(&latch);
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

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp timeout blocking") {
  a0::RpcClient client("test");

  REQUIRE_THROWS_WITH(
      client.send_blocking("request", a0::TimeMono::now()),
      a0_strerror(A0_ERR_TIMEDOUT));
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] cpp timeout order") {
  a0_latch_t latch;
  a0_latch_init(&latch, 5);

  a0::RpcClient client("test");
  std::vector<int> timeout_order;

  for (int i = 0; i < 5; i++) {
    a0::RpcClient::SendOptions opts = A0_EMPTY;
    opts.timeout = a0::TimeMono::now() + std::chrono::milliseconds(i * 10);
    opts.ontimeout = [&, i]() {
      timeout_order.push_back(i);
      a0_latch_count_down(&latch, 1);
    };
    client.send("", nullptr, opts);
  }

  a0_latch_wait(&latch);

  REQUIRE(timeout_order == std::vector<int>{0, 1, 2, 3, 4});
}
