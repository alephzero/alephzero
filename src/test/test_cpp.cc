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
    a0::Shm::unlink(shm.path());
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

    REQUIRE(pkt_view.num_headers() == 3);

    std::map<std::string, std::string> hdrs;
    for (size_t i = 0; i < pkt_view.num_headers(); i++) {
      auto hdr = pkt_view.header(i);
      hdrs[std::string(hdr.first)] = hdr.second;
    }
    REQUIRE(hdrs.count("a0_id"));
    REQUIRE(hdrs.count("a0_mono_time"));
    REQUIRE(hdrs.count("a0_wall_time"));

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

  a0::sync<std::vector<std::string>> read_payloads;
  a0::Subscriber sub(shm, A0_INIT_OLDEST, A0_ITER_NEXT, [&](a0::PacketView pkt_view) {
    read_payloads.notify_one([&](auto* payloads) {
      payloads->push_back(std::string(pkt_view.payload()));
    });
  });

  read_payloads.wait([&](auto* payloads) {
    return payloads->size() == 2;
  });

  read_payloads.with_lock([&](auto* payloads) {
    REQUIRE((*payloads)[0] == "msg #0");
    REQUIRE((*payloads)[1] == "msg #1");
  });

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
  a0::Event cancel_event;
  auto oncancel = [&](std::string id) {
    REQUIRE(id == cancel_pkt.id());
    cancel_event.set();
  };
  a0::RpcServer server(shm, onrequest, oncancel);

  a0::RpcClient client(shm);
  REQUIRE(client.send("foo").get().payload() == "bar");

  a0::Event evt;
  client.send("foo", [&](a0::PacketView pkt_view) {
    REQUIRE(pkt_view.payload() == "bar");
    evt.set();
  });
  evt.wait();

  client.cancel(cancel_pkt.id());
  cancel_event.wait();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test rpc null callback") {
  a0::Event req_evt;
  auto onrequest = [&](a0::RpcRequest) {
    req_evt.set();
  };
  a0::RpcServer server(shm, onrequest, nullptr);

  a0::RpcClient client(shm);
  client.cancel("D4D4BA13-400E-48D3-8FC7-470A0498B60B");

  // TODO: be better!
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  client.send("foo", nullptr);
  req_evt.wait();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test prpc") {
  auto onconnect = [](a0::PrpcConnection conn) {
    REQUIRE(conn.pkt().payload() == "foo");
    conn.send("msg #0", false);
    conn.send("msg #1", false);
    conn.send("msg #2", true);
  };

  a0::Packet cancel_pkt("");
  a0::Event cancel_event;
  auto oncancel = [&](std::string id) {
    REQUIRE(id == cancel_pkt.id());
    cancel_event.set();
  };

  a0::PrpcServer server(shm, onconnect, oncancel);

  a0::PrpcClient client(shm);

  std::vector<std::string> msgs;
  a0::Event done_event;
  client.connect("foo", [&](a0::PacketView pkt_view, bool done) {
    msgs.push_back(std::string(pkt_view.payload()));
    if (done) {
      done_event.set();
    }
  });
  done_event.wait();

  REQUIRE(msgs.size() == 3);
  REQUIRE(msgs[0] == "msg #0");
  REQUIRE(msgs[1] == "msg #1");
  REQUIRE(msgs[2] == "msg #2");

  client.cancel(cancel_pkt.id());
  cancel_event.wait();
}

TEST_CASE_FIXTURE(CppPubsubFixture, "Test prpc null callback") {
  auto onconnect = [](a0::PrpcConnection conn) {
    conn.send("msg", true);
  };

  a0::PrpcServer server(shm, onconnect, nullptr);

  a0::PrpcClient client(shm);

  client.cancel("D4D4BA13-400E-48D3-8FC7-470A0498B60B");

  // TODO: be better!
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
