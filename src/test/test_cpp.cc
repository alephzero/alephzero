#include <a0/alephzero.hpp>

#include <doctest.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";

struct CppPubsubFixture {
  a0::Shm shm;

  CppPubsubFixture() {
    a0::Shm::unlink(kTestShm);

    shm = a0::Shm(kTestShm, a0::Shm::Options{.size = 16 * 1024 * 1024});
  }

  ~CppPubsubFixture() {
    a0::Shm::unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(CppPubsubFixture, "Test shm") {
  REQUIRE(shm.path() == kTestShm);

  shm = a0::Shm(kTestShm);
  REQUIRE(shm.path() == kTestShm);
  REQUIRE(shm.c->buf.size == 16 * 1024 * 1024);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test pkt") {
  a0::Packet pkt({{"hdr-key", "hdr-val"}}, "Hello, World!");
  REQUIRE(pkt.payload() == "Hello, World!");
  REQUIRE(pkt.num_headers() == 2);
  REQUIRE(pkt.id().size() == 36);

  REQUIRE(pkt.header(0).first == "a0_id");
  REQUIRE(pkt.header(1).first == "hdr-key");
  REQUIRE(pkt.header(1).second == "hdr-val");

  a0::PacketView pkt_view{.c = pkt.c()};
  REQUIRE(pkt.id() == pkt_view.id());

  a0::Packet pkt2 = pkt_view;
  REQUIRE(pkt.c().ptr != pkt2.c().ptr);
  REQUIRE(pkt.id() == pkt2.id());
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test topic manager") {
  REQUIRE_THROWS_WITH(a0::TopicManager(R"({ zzz })"), "Invalid argument");

  a0::TopicManager tm(R"({
    "container": "aaa",
    "subscriber_maps": {
      "subby": {
        "container": "bbb",
        "topic": "foo"
      }
    },
    "rpc_client_maps": {
      "rpcy": {
        "container": "bbb",
        "topic": "bar"
      }
    },
    "prpc_client_maps": {
      "prpcy": {
        "container": "ccc",
        "topic": "bat"
      }
    }
  })");

  auto REQUIRE_PATH = [&](a0::Shm shm, std::string_view expected_path) {
    REQUIRE(shm.path() == expected_path);
  };

  REQUIRE_PATH(tm.config_topic(), "/a0_config__aaa");
  REQUIRE_PATH(tm.publisher_topic("baz"), "/a0_pubsub__aaa__baz");
  REQUIRE_PATH(tm.subscriber_topic("subby"), "/a0_pubsub__bbb__foo");
  REQUIRE_PATH(tm.rpc_server_topic("alice"), "/a0_rpc__aaa__alice");
  REQUIRE_PATH(tm.rpc_client_topic("rpcy"), "/a0_rpc__bbb__bar");
  REQUIRE_PATH(tm.prpc_server_topic("bob"), "/a0_prpc__aaa__bob");
  REQUIRE_PATH(tm.prpc_client_topic("prpcy"), "/a0_prpc__ccc__bat");

  REQUIRE_THROWS_WITH(tm.subscriber_topic("not_subby"), "Invalid argument");
  REQUIRE_THROWS_WITH(tm.rpc_client_topic("not_rpcy"), "Invalid argument");
  REQUIRE_THROWS_WITH(tm.prpc_client_topic("not_prpcy"), "Invalid argument");
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test pubsub sync") {
  a0::Publisher p(shm);
  p.pub("msg #0");
  p.pub("msg #1");

  {
    a0::SubscriberSync sub(shm, A0_INIT_OLDEST, A0_ITER_NEXT);

    REQUIRE(sub.has_next());
    auto pkt_view = sub.next();

    REQUIRE(pkt_view.num_headers() == 2);

    std::map<std::string, std::string> hdrs;
    for (size_t i = 0; i < pkt_view.num_headers(); i++) {
      auto hdr = pkt_view.header(i);
      hdrs[std::string(hdr.first)] = hdr.second;
    }
    REQUIRE(hdrs.count("a0_id"));
    REQUIRE(hdrs.count("a0_clock"));

    REQUIRE(pkt_view.payload() == "msg #0");

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #1");

    REQUIRE(!sub.has_next());
  }

  {
    a0::SubscriberSync sub(shm, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #1");

    REQUIRE(!sub.has_next());
  }
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test pubsub") {
  a0::Publisher p(shm);
  p.pub("msg #0");
  p.pub("msg #1");

  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::string> read_payloads;
  a0::Subscriber sub(shm, A0_INIT_OLDEST, A0_ITER_NEXT, [&](a0::PacketView pkt_view) {
    std::unique_lock lk(mu);
    read_payloads.push_back(std::string(pkt_view.payload()));
    cv.notify_one();
  });

  std::unique_lock lk(mu);
  cv.wait(lk, [&]() {
    return read_payloads.size() == 2;
  });

  REQUIRE(read_payloads[0] == "msg #0");
  REQUIRE(read_payloads[1] == "msg #1");

  {
    auto pkt = a0::Subscriber::read_one(shm, A0_INIT_OLDEST, O_NONBLOCK);
    REQUIRE(pkt.payload() == "msg #0");
  }
  {
    auto pkt = a0::Subscriber::read_one(shm, A0_INIT_MOST_RECENT, O_NONBLOCK);
    REQUIRE(pkt.payload() == "msg #1");
  }
  REQUIRE_THROWS_WITH(a0::Subscriber::read_one(shm, A0_INIT_AWAIT_NEW, O_NONBLOCK),
                      "Resource temporarily unavailable");
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test rpc") {
  auto onrequest = [](a0::RpcRequest req) {
    REQUIRE(req.pkt().payload() == "foo");
    req.reply("bar");
  };

  a0::Packet cancel_pkt("");
  std::mutex cancel_mu;
  auto oncancel = [&](std::string id) {
    REQUIRE(id == cancel_pkt.id());
    cancel_mu.unlock();
  };
  a0::RpcServer server(shm, onrequest, oncancel);

  a0::RpcClient client(shm);
  REQUIRE(client.send("foo").get().payload() == "bar");

  std::mutex mu;
  mu.lock();
  client.send("foo", [&](a0::PacketView pkt_view) {
    REQUIRE(pkt_view.payload() == "bar");
    mu.unlock();
  });
  mu.lock();

  cancel_mu.lock();
  client.cancel(cancel_pkt.id());
  cancel_mu.lock();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test rpc null callback") {
  std::mutex req_mu;
  auto onrequest = [&](a0::RpcRequest) {
    req_mu.unlock();
  };
  a0::RpcServer server(shm, onrequest, nullptr);

  a0::RpcClient client(shm);
  client.cancel("pkt_id");

  // TODO: be better!
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  req_mu.lock();
  client.send("foo", nullptr);
  req_mu.lock();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test prpc") {
  auto onconnect = [](a0::PrpcConnection conn) {
    REQUIRE(conn.pkt().payload() == "foo");
    conn.send("msg #0", false);
    conn.send("msg #1", false);
    conn.send("msg #2", true);
  };

  a0::Packet cancel_pkt("");
  std::mutex cancel_mu;
  auto oncancel = [&](std::string id) {
    REQUIRE(id == cancel_pkt.id());
    cancel_mu.unlock();
  };

  a0::PrpcServer server(shm, onconnect, oncancel);

  a0::PrpcClient client(shm);

  std::vector<std::string> msgs;
  std::mutex mu;
  mu.lock();
  client.connect("foo", [&](a0::PacketView pkt_view, bool done) {
    msgs.push_back(std::string(pkt_view.payload()));
    if (done) {
      mu.unlock();
    }
  });
  mu.lock();

  REQUIRE(msgs.size() == 3);
  REQUIRE(msgs[0] == "msg #0");
  REQUIRE(msgs[1] == "msg #1");
  REQUIRE(msgs[2] == "msg #2");

  cancel_mu.lock();
  client.cancel(cancel_pkt.id());
  cancel_mu.lock();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test prpc null callback") {
  auto onconnect = [](a0::PrpcConnection conn) {
    conn.send("msg", true);
  };

  a0::PrpcServer server(shm, onconnect, nullptr);

  a0::PrpcClient client(shm);

  client.cancel("pkt_id");

  // TODO: be better!
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
