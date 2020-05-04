#include <a0/alephzero.hpp>
#include <a0/common.h>
#include <a0/pubsub.h>
#include <a0/shm.h>

#include <doctest.h>
#include <fcntl.h>

#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "src/sync.hpp"

static const char TEST_SHM[] = "/test.shm";

struct CppPubsubFixture {
  a0::Shm shm;

  CppPubsubFixture() {
    a0::Shm::unlink(TEST_SHM);

    shm = a0::Shm(TEST_SHM);

    a0::GlobalTopicManager() = {};
  }

  ~CppPubsubFixture() {
    a0::Shm::unlink(TEST_SHM);
  }
};

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] shm") {
  REQUIRE(shm.path() == TEST_SHM);

  shm = a0::Shm(TEST_SHM);
  REQUIRE(shm.path() == TEST_SHM);
  REQUIRE(shm.c->buf.size == A0_SHM_OPTIONS_DEFAULT.size);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pkt") {
  a0::Packet pkt({{"hdr-key", "hdr-val"}}, "Hello, World!");
  REQUIRE(pkt.payload() == "Hello, World!");
  REQUIRE(pkt.headers().size() == 1);
  REQUIRE(pkt.id().size() == 36);

  REQUIRE(pkt.headers()[0].first == "hdr-key");
  REQUIRE(pkt.headers()[0].second == "hdr-val");

  a0::PacketView pkt_view = pkt;
  REQUIRE(pkt.id() == pkt_view.id());
  REQUIRE(pkt.headers() == pkt_view.headers());
  REQUIRE(pkt.payload() == pkt_view.payload());
  REQUIRE(pkt.payload().data() == pkt_view.payload().data());

  a0::Packet pkt2 = pkt_view;
  REQUIRE(pkt2.id() == pkt_view.id());
  REQUIRE(pkt.id() == pkt2.id());
  REQUIRE(pkt.headers() == pkt2.headers());
  REQUIRE(pkt.payload() == pkt2.payload());
  REQUIRE(pkt.payload().data() != pkt2.payload().data());
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] topic manager") {
  a0::TopicManager tm = {
      .container = "aaa",
      .subscriber_aliases =
          {
              {"subby", {.container = "bbb", .topic = "foo"}},
          },
      .rpc_client_aliases =
          {
              {"rpcy", {.container = "bbb", .topic = "bar"}},
          },
      .prpc_client_aliases =
          {
              {"prpcy", {.container = "ccc", .topic = "bat"}},
          },
  };

  auto REQUIRE_PATH = [&](a0::Shm shm, std::string_view expected_path) {
    REQUIRE(shm.path() == expected_path);
    a0::Shm::unlink(shm.path());
  };

  REQUIRE_PATH(tm.config_topic(), "/a0_config__aaa");
  REQUIRE_PATH(tm.heartbeat_topic(), "/a0_heartbeat__aaa");
  REQUIRE_PATH(tm.log_crit_topic(), "/a0_log_crit__aaa");
  REQUIRE_PATH(tm.log_err_topic(), "/a0_log_err__aaa");
  REQUIRE_PATH(tm.log_warn_topic(), "/a0_log_warn__aaa");
  REQUIRE_PATH(tm.log_info_topic(), "/a0_log_info__aaa");
  REQUIRE_PATH(tm.log_dbg_topic(), "/a0_log_dbg__aaa");
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

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] config") {
  a0::Shm::unlink("/a0_config__test");
  a0::Shm::unlink("/a0_config__test_other");

  a0::InitGlobalTopicManager(a0::TopicManager{.container = "test"});

  a0::write_config(a0::GlobalTopicManager(), R"({"foo": "aaa"})");
  a0::write_config(a0::TopicManager{.container = "test_other"}, R"({"foo": "bbb"})");

  REQUIRE(a0::read_config().payload() == R"({"foo": "aaa"})");
  a0::GlobalTopicManager().container = "test_other";
  REQUIRE(a0::read_config().payload() == R"({"foo": "bbb"})");

  a0::write_config(a0::GlobalTopicManager(), R"({"foo": "ccc"})");
  REQUIRE(a0::read_config().payload() == R"({"foo": "ccc"})");

  a0::Shm::unlink("/a0_config__test");
  a0::Shm::unlink("/a0_config__test_other");
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pubsub sync") {
  a0::Publisher p(shm);

  p.pub("msg #0");
  p.pub(std::string("msg #1"));
  p.pub(a0::PacketView({{"key", "val"}}, "msg #2"));
  p.pub(a0::Packet({{"key", "val"}}, "msg #3"));

  {
    a0::SubscriberSync sub(shm, A0_INIT_OLDEST, A0_ITER_NEXT);

    REQUIRE(sub.has_next());
    auto pkt_view = sub.next();

    {
      std::set<std::string> hdr_keys;
      for (auto&& kv : pkt_view.headers()) {
        hdr_keys.insert(kv.first);
      }
      REQUIRE(hdr_keys == std::set<std::string>{"a0_time_mono",
                                                "a0_time_wall",
                                                "a0_transport_seq",
                                                "a0_publisher_seq",
                                                "a0_publisher_id"});
    }

    REQUIRE(pkt_view.payload() == "msg #0");

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #1");

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #2");

    REQUIRE(sub.has_next());
    pkt_view = sub.next();
    REQUIRE(pkt_view.payload() == "msg #3");
    {
      std::set<std::string> hdr_keys;
      for (auto&& kv : pkt_view.headers()) {
        hdr_keys.insert(kv.first);
      }
      REQUIRE(hdr_keys == std::set<std::string>{"key",
                                                "a0_time_mono",
                                                "a0_time_wall",
                                                "a0_transport_seq",
                                                "a0_publisher_seq",
                                                "a0_publisher_id"});
    }

    REQUIRE(!sub.has_next());
  }

  {
    a0::SubscriberSync sub(shm, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #3");

    REQUIRE(!sub.has_next());
  }
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pubsub") {
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

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] rpc") {
  auto onrequest = [](a0::RpcRequest req) {
    REQUIRE(req.pkt().payload() == "foo");
    req.reply("bar");
  };

  a0::Packet cancel_pkt("");
  a0::Event cancel_event;
  auto oncancel = [&](const std::string_view id) {
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

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] rpc null callback") {
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

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] prpc") {
  auto onconnect = [](a0::PrpcConnection conn) {
    REQUIRE(conn.pkt().payload() == "foo");
    conn.send("msg #0", false);
    conn.send("msg #1", false);
    conn.send("msg #2", true);
  };

  a0::Packet cancel_pkt;
  a0::Event cancel_event;
  auto oncancel = [&](const std::string_view id) {
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

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] prpc null callback") {
  auto onconnect = [](a0::PrpcConnection conn) {
    conn.send("msg", true);
  };

  a0::PrpcServer server(shm, onconnect, nullptr);

  a0::PrpcClient client(shm);

  client.cancel("D4D4BA13-400E-48D3-8FC7-470A0498B60B");

  // TODO: be better!
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hb start, hbl start, hbl close, hb close") {
  auto hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  a0::Subscriber::read_one(shm, A0_INIT_MOST_RECENT, 0);

  int detected_cnt = 0;
  int missed_cnt = 0;

  auto hbl = std::make_unique<a0::HeartbeatListener>(
      shm,
      a0::HeartbeatListener::Options{.min_freq = 50},
      [&]() { detected_cnt++; },
      [&]() { missed_cnt++; });

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  hbl = nullptr;

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hb start, hbl start, hb close, hbl close") {
  auto hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  a0::Subscriber::read_one(shm, A0_INIT_MOST_RECENT, 0);

  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  auto hbl = std::make_unique<a0::HeartbeatListener>(
      shm,
      a0::HeartbeatListener::Options{.min_freq = 50},
      [&]() { detected_cnt++; },
      [&]() { missed_cnt++; });

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  hb = nullptr;

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hbl start, hb start, hb close, hbl close") {
  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  auto hbl = std::make_unique<a0::HeartbeatListener>(
      shm,
      a0::HeartbeatListener::Options{.min_freq = 50},
      [&]() { detected_cnt++; },
      [&]() { missed_cnt++; });

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 0);
  REQUIRE(missed_cnt == 0);

  auto hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);

  hb = nullptr;

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 1);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat ignore old") {
  auto hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  a0::Subscriber::read_one(shm, A0_INIT_MOST_RECENT, 0);

  hb = nullptr;

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  // At this point, a heartbeat is written, but old.

  std::atomic<int> detected_cnt = 0;
  std::atomic<int> missed_cnt = 0;

  auto hbl = std::make_unique<a0::HeartbeatListener>(
      shm,
      a0::HeartbeatListener::Options{.min_freq = 50},
      [&]() { detected_cnt++; },
      [&]() { missed_cnt++; });

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 0);
  REQUIRE(missed_cnt == 0);

  hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  std::this_thread::sleep_for(std::chrono::nanoseconds(uint64_t(1e9 / 10)));

  REQUIRE(detected_cnt == 1);
  REQUIRE(missed_cnt == 0);
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat listener async close") {
  auto hb = std::make_unique<a0::Heartbeat>(shm, a0::Heartbeat::Options{.freq = 100});

  a0::Event init_event;
  a0::Event stop_event;

  std::unique_ptr<a0::HeartbeatListener> hbl;
  hbl = std::make_unique<a0::HeartbeatListener>(
      shm,
      a0::HeartbeatListener::Options{.min_freq = 50},
      [&]() {
        init_event.set();
        hbl->async_close([&]() {
          hbl = nullptr;
          stop_event.set();
        });
      },
      nullptr);
  REQUIRE(init_event.wait_for(std::chrono::nanoseconds(uint64_t(1e9 / 10))) ==
          std::cv_status::no_timeout);
  REQUIRE(stop_event.wait_for(std::chrono::nanoseconds(uint64_t(1e9 / 10))) ==
          std::cv_status::no_timeout);
  hbl = nullptr;
}
