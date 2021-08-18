#include <a0/alephzero.hpp>
// #include <a0/file.h>
// #include <a0/pubsub.h>

#include <doctest.h>
// #include <fcntl.h>
// #include <sys/types.h>

// #include <algorithm>
// #include <atomic>
// #include <chrono>
// #include <condition_variable>
// #include <cstddef>
// #include <cstdint>
// #include <cstdlib>
// #include <exception>
// #include <functional>
// #include <future>
// #include <limits>
// #include <memory>
// #include <set>
// #include <stdexcept>
// #include <string>
// #include <string_view>
// #include <thread>
// #include <utility>
// #include <vector>

// #include "src/sync.hpp"
// #include "src/test_util.hpp"

static const char TEST_FILE[] = "cpp_test.file";

static constexpr size_t MB = 1024 * 1024;

struct CppPubsubFixture {
  a0::File file;

  CppPubsubFixture() {
    cleanup();

    file = a0::File(TEST_FILE);
  }

  ~CppPubsubFixture() {
    cleanup();
  }

  void cleanup() {
    a0::File::remove(TEST_FILE);
    unsetenv("A0_ROOT");
  }
};

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] file") {
  REQUIRE(file.path() == "/dev/shm/cpp_test.file");
  REQUIRE(file.size() == A0_FILE_OPTIONS_DEFAULT.create_options.size);
  REQUIRE(file.size() == a0::Buf(a0::Arena(file)).size());
  REQUIRE(file.size() == a0::Buf(file).size());

  a0::Arena arena;
  {
    a0::File file2(TEST_FILE);
    arena = file2;
  }
  REQUIRE(file.size() == arena.buf().size());

  file = {};
  a0::File::remove(TEST_FILE);

  a0::File::Options opts = a0::File::Options::DEFAULT;
  opts.create_options.size = 32 * MB;

  file = a0::File(TEST_FILE, opts);
  REQUIRE(file.size() == 32 * MB);

  file = {};
  a0::File::remove(TEST_FILE);

  opts.create_options.size = std::numeric_limits<off_t>::max();

  try {
    file = a0::File(TEST_FILE, opts);
  } catch (const std::exception& e) {
    std::string err = e.what();
    REQUIRE((err == "Cannot allocate memory" ||
             err == "File too large" ||
             err == "Invalid argument" ||
             err == "Out of memory"));
  }

  file = {};
  a0::File::remove(TEST_FILE);

  opts.create_options.size = -1;

  REQUIRE_THROWS_WITH(
      [&]() { file = a0::File(TEST_FILE, opts); }(),
      "Invalid argument");

  REQUIRE_THROWS_WITH(
      [&]() {
        file = a0::File();
        file.size();
      }(),
      "AlephZero method called with NULL object: size_t a0::File::size() const");
}

TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pkt") {
  a0::Packet pkt1({{"hdr-key", "hdr-val"}}, "Hello, World!");
  REQUIRE(pkt1.payload() == "Hello, World!");
  REQUIRE(pkt1.headers().size() == 1);
  REQUIRE(pkt1.id().size() == 36);

  REQUIRE(pkt1.headers()[0].first == "hdr-key");
  REQUIRE(pkt1.headers()[0].second == "hdr-val");

  a0::Packet pkt2 = pkt1;
  REQUIRE(pkt2.id() == pkt1.id());
  REQUIRE(pkt1.id() == pkt2.id());
  REQUIRE(pkt1.headers() == pkt2.headers());
  REQUIRE(pkt1.payload() == pkt2.payload());
  REQUIRE(pkt1.payload().data() == pkt2.payload().data());

  a0::Packet pkt3("Hello, World!");
  REQUIRE(pkt3.payload() == "Hello, World!");
  REQUIRE(pkt3.headers().empty());
  REQUIRE(pkt3.id().size() == 36);

  std::string owner = "Hello, World!";

  a0::Packet pkt4(owner);
  REQUIRE(pkt4.payload() == owner);
  REQUIRE(pkt4.payload().data() != owner.data());

  a0::Packet pkt5(owner, a0::ref);
  REQUIRE(pkt5.payload() == owner);
  REQUIRE(pkt5.payload().data() == owner.data());
}

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] config") {
//   a0::File::remove("a0_config__test");
//   a0::File::remove("a0_config__test_other");

//   a0::InitGlobalTopicManager(a0::TopicManager{
//       .container = "test",
//       .subscriber_aliases = {},
//       .rpc_client_aliases = {},
//       .prpc_client_aliases = {},
//   });

//   a0::write_config(a0::GlobalTopicManager(), R"({"foo": "aaa"})");
//   a0::write_config(
//       a0::TopicManager{
//           .container = "test_other",
//           .subscriber_aliases = {},
//           .rpc_client_aliases = {},
//           .prpc_client_aliases = {},
//       },
//       R"({"foo": "bbb"})");

//   REQUIRE(a0::read_config().payload() == R"({"foo": "aaa"})");
//   a0::GlobalTopicManager().container = "test_other";
//   REQUIRE(a0::read_config().payload() == R"({"foo": "bbb"})");

//   a0::write_config(a0::GlobalTopicManager(), R"({"foo": "ccc"})");
//   REQUIRE(a0::read_config().payload() == R"({"foo": "ccc"})");

//   a0::File::remove("a0_config__test");
//   a0::File::remove("a0_config__test_other");
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pubsub raw sync") {
//   a0::PublisherRaw p(file);

//   p.pub("msg #0");
//   p.pub(std::string("msg #1"));
//   p.pub(a0::PacketView({{"key", "val"}}, "msg #2"));
//   p.pub(a0::Packet({{"key", "val"}}, "msg #3"));

//   {
//     a0::SubscriberSync sub(file, A0_INIT_OLDEST, A0_ITER_NEXT);

//     REQUIRE(sub.has_next());
//     auto pkt_view = sub.next();

//     {
//       std::set<std::string> hdr_keys;
//       for (auto&& kv : pkt_view.headers()) {
//         hdr_keys.insert(kv.first);
//       }
//       REQUIRE(hdr_keys.empty());
//     }

//     REQUIRE(pkt_view.payload() == "msg #0");

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #1");

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #2");

//     REQUIRE(sub.has_next());
//     pkt_view = sub.next();
//     REQUIRE(pkt_view.payload() == "msg #3");
//     {
//       std::set<std::string> hdr_keys;
//       for (auto&& kv : pkt_view.headers()) {
//         hdr_keys.insert(kv.first);
//       }
//       REQUIRE(hdr_keys == std::set<std::string>{"key"});
//     }

//     REQUIRE(!sub.has_next());
//   }

//   {
//     a0::SubscriberSync sub(file, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #3");

//     REQUIRE(!sub.has_next());
//   }
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pubsub sync") {
//   a0::Publisher p(file);

//   p.pub("msg #0");
//   p.pub(std::string("msg #1"));
//   p.pub(a0::PacketView({{"key", "val"}}, "msg #2"));
//   p.pub(a0::Packet({{"key", "val"}}, "msg #3"));

//   {
//     a0::SubscriberSync sub(file, A0_INIT_OLDEST, A0_ITER_NEXT);

//     REQUIRE(sub.has_next());
//     auto pkt_view = sub.next();

//     {
//       std::set<std::string> hdr_keys;
//       for (auto&& kv : pkt_view.headers()) {
//         hdr_keys.insert(kv.first);
//       }
//       REQUIRE(hdr_keys == std::set<std::string>{"a0_time_mono",
//                                                 "a0_time_wall",
//                                                 // "a0_transport_seq",
//                                                 "a0_publisher_seq",
//                                                 "a0_publisher_id"});
//     }

//     REQUIRE(pkt_view.payload() == "msg #0");

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #1");

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #2");

//     REQUIRE(sub.has_next());
//     pkt_view = sub.next();
//     REQUIRE(pkt_view.payload() == "msg #3");
//     {
//       std::set<std::string> hdr_keys;
//       for (auto&& kv : pkt_view.headers()) {
//         hdr_keys.insert(kv.first);
//       }
//       REQUIRE(hdr_keys == std::set<std::string>{"key",
//                                                 "a0_time_mono",
//                                                 "a0_time_wall",
//                                                 // "a0_transport_seq",
//                                                 "a0_publisher_seq",
//                                                 "a0_publisher_id"});
//     }

//     REQUIRE(!sub.has_next());
//   }

//   {
//     a0::SubscriberSync sub(file, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

//     REQUIRE(sub.has_next());
//     REQUIRE(sub.next().payload() == "msg #3");

//     REQUIRE(!sub.has_next());
//   }
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] pubsub") {
//   a0::Publisher p(file);
//   p.pub("msg #0");
//   p.pub("msg #1");

//   a0::sync<std::vector<std::string>> read_payloads;
//   a0::Subscriber sub(file, A0_INIT_OLDEST, A0_ITER_NEXT, [&](a0::PacketView pkt_view) {
//     read_payloads.notify_one([&](auto* payloads) {
//       payloads->push_back(std::string(pkt_view.payload()));
//     });
//   });

//   read_payloads.wait([&](auto* payloads) {
//     return payloads->size() == 2;
//   });

//   read_payloads.with_lock([&](auto* payloads) {
//     REQUIRE((*payloads)[0] == "msg #0");
//     REQUIRE((*payloads)[1] == "msg #1");
//   });

//   {
//     auto pkt = a0::Subscriber::read_one(file, A0_INIT_OLDEST, O_NONBLOCK);
//     REQUIRE(pkt.payload() == "msg #0");
//   }
//   {
//     auto pkt = a0::Subscriber::read_one(file, A0_INIT_MOST_RECENT, O_NONBLOCK);
//     REQUIRE(pkt.payload() == "msg #1");
//   }
//   REQUIRE_THROWS_WITH(a0::Subscriber::read_one(file, A0_INIT_AWAIT_NEW, O_NONBLOCK),
//                       "Resource temporarily unavailable");
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] sub throw") {
//   REQUIRE_SIGNAL({
//     a0::Publisher p(file);
//     p.pub("");
//     a0::Subscriber sub(file, A0_INIT_OLDEST, A0_ITER_NEXT, [&](a0::PacketView) {
//       throw std::runtime_error("FOOBAR");
//     });
//     std::this_thread::sleep_for(std::chrono::seconds(1));
//   });
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] rpc") {
//   auto onrequest = [](a0::RpcRequest req) {
//     REQUIRE(req.pkt().payload() == "foo");
//     req.reply("bar");
//   };

//   a0::Packet cancel_pkt("");
//   a0::Event cancel_event;
//   auto oncancel = [&](std::string_view id) {
//     REQUIRE(id == cancel_pkt.id());
//     cancel_event.set();
//   };
//   a0::RpcServer server(file, onrequest, oncancel);

//   a0::RpcClient client(file);
//   REQUIRE(client.send("foo").get().payload() == "bar");

//   a0::Event evt;
//   client.send("foo", [&](a0::PacketView pkt_view) {
//     REQUIRE(pkt_view.payload() == "bar");
//     evt.set();
//   });
//   evt.wait();

//   client.cancel(cancel_pkt.id());
//   cancel_event.wait();
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] rpc null callback") {
//   a0::Event req_evt;
//   auto onrequest = [&](a0::RpcRequest) {
//     req_evt.set();
//   };
//   a0::RpcServer server(file, onrequest, nullptr);

//   a0::RpcClient client(file);
//   client.cancel("D4D4BA13-400E-48D3-8FC7-470A0498B60B");

//   // TODO: be better!
//   std::this_thread::sleep_for(std::chrono::milliseconds(1));

//   client.send("foo", nullptr);
//   req_evt.wait();
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] prpc") {
//   auto onconnect = [](a0::PrpcConnection conn) {
//     REQUIRE(conn.pkt().payload() == "foo");
//     conn.send("msg #0", false);
//     conn.send("msg #1", false);
//     conn.send("msg #2", true);
//   };

//   a0::Packet cancel_pkt;
//   a0::Event cancel_event;
//   auto oncancel = [&](std::string_view id) {
//     REQUIRE(id == cancel_pkt.id());
//     cancel_event.set();
//   };

//   a0::PrpcServer server(file, onconnect, oncancel);

//   a0::PrpcClient client(file);

//   std::vector<std::string> msgs;
//   a0::Event done_event;
//   client.connect("foo", [&](a0::PacketView pkt_view, bool done) {
//     msgs.emplace_back(pkt_view.payload());
//     if (done) {
//       done_event.set();
//     }
//   });
//   done_event.wait();

//   REQUIRE(msgs.size() == 3);
//   REQUIRE(msgs[0] == "msg #0");
//   REQUIRE(msgs[1] == "msg #1");
//   REQUIRE(msgs[2] == "msg #2");

//   client.cancel(cancel_pkt.id());
//   cancel_event.wait();
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] prpc null callback") {
//   auto onconnect = [](a0::PrpcConnection conn) {
//     conn.send("msg", true);
//   };

//   a0::PrpcServer server(file, onconnect, nullptr);

//   a0::PrpcClient client(file);

//   client.cancel("D4D4BA13-400E-48D3-8FC7-470A0498B60B");

//   // TODO: be better!
//   std::this_thread::sleep_for(std::chrono::milliseconds(1));
// }

// a0::Heartbeat::Options TestHeartbeatOptions() {
//   return a0::Heartbeat::Options{.freq = 100};
// }

// a0::HeartbeatListener::Options TestHeartbeatListenerOptions() {
//   if (a0::test::is_debug_mode()) {
//     return a0::HeartbeatListener::Options{.min_freq = 25};
//   }
//   return a0::HeartbeatListener::Options{.min_freq = 90};
// }

// std::chrono::nanoseconds heartbeat_sync_duration() {
//   if (a0::test::is_debug_mode()) {
//     return std::chrono::nanoseconds(uint64_t(1e9 / 10));
//   }
//   return std::chrono::nanoseconds(uint64_t(1e9 / 40));
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hb start, hbl start, hbl close, hb close") {
//   auto hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   a0::Subscriber::read_one(file, A0_INIT_MOST_RECENT, 0);

//   int detected_cnt = 0;
//   int missed_cnt = 0;

//   auto hbl = std::make_unique<a0::HeartbeatListener>(
//       file,
//       TestHeartbeatListenerOptions(),
//       [&]() { detected_cnt++; },
//       [&]() { missed_cnt++; });

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   hbl = nullptr;

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 0);
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hb start, hbl start, hb close, hbl close") {
//   auto hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   a0::Subscriber::read_one(file, A0_INIT_MOST_RECENT, 0);

//   std::atomic<int> detected_cnt = 0;
//   std::atomic<int> missed_cnt = 0;

//   auto hbl = std::make_unique<a0::HeartbeatListener>(
//       file,
//       TestHeartbeatListenerOptions(),
//       [&]() { detected_cnt++; },
//       [&]() { missed_cnt++; });

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 0);

//   hb = nullptr;

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 1);
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat hbl start, hb start, hb close, hbl close") {
//   std::atomic<int> detected_cnt = 0;
//   std::atomic<int> missed_cnt = 0;

//   auto hbl = std::make_unique<a0::HeartbeatListener>(
//       file,
//       TestHeartbeatListenerOptions(),
//       [&]() { detected_cnt++; },
//       [&]() { missed_cnt++; });

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 0);
//   REQUIRE(missed_cnt == 0);

//   auto hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 0);

//   hb = nullptr;

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 1);
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat ignore old") {
//   auto hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   a0::Subscriber::read_one(file, A0_INIT_MOST_RECENT, 0);

//   hb = nullptr;

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   // At this point, a heartbeat is written, but old.

//   std::atomic<int> detected_cnt = 0;
//   std::atomic<int> missed_cnt = 0;

//   auto hbl = std::make_unique<a0::HeartbeatListener>(
//       file,
//       TestHeartbeatListenerOptions(),
//       [&]() { detected_cnt++; },
//       [&]() { missed_cnt++; });

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 0);
//   REQUIRE(missed_cnt == 0);

//   hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   std::this_thread::sleep_for(heartbeat_sync_duration());

//   REQUIRE(detected_cnt == 1);
//   REQUIRE(missed_cnt == 0);
// }

// TEST_CASE_FIXTURE(CppPubsubFixture, "cpp] heartbeat listener async close") {
//   auto hb = std::make_unique<a0::Heartbeat>(file, TestHeartbeatOptions());

//   a0::Event init_event;
//   a0::Event stop_event;

//   std::unique_ptr<a0::HeartbeatListener> hbl;
//   hbl = std::make_unique<a0::HeartbeatListener>(
//       file,
//       TestHeartbeatListenerOptions(),
//       [&]() {
//         REQUIRE(init_event.wait_for(heartbeat_sync_duration()) ==
//                 std::cv_status::no_timeout);
//         hbl->async_close([&]() {
//           hbl = nullptr;
//           stop_event.set();
//         });
//       },
//       nullptr);
//   init_event.set();
//   REQUIRE(stop_event.wait_for(heartbeat_sync_duration()) ==
//           std::cv_status::no_timeout);
//   REQUIRE(!hbl);
// }
