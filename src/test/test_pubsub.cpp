#include <a0/buf.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/pubsub.h>

#include <doctest.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "src/strutil.hpp"
#include "src/sync.hpp"
#include "src/test_util.hpp"

static const char TEST_TOPIC[] = "test";
static const char TEST_FILE[] = "alephzero/test.pubsub.a0";

struct PubsubFixture {
  a0_file_t file;

  PubsubFixture() {
    a0_file_remove(TEST_FILE);

    a0_file_open(TEST_FILE, nullptr, &file);
  }

  ~PubsubFixture() {
    a0_file_close(&file);
    a0_file_remove(TEST_FILE);
  }
};

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] sync") {
//   {
//     a0_publisher_t pub;
//     REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));

//     a0_packet_header_t hdr = {"key", "val"};

//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet(&hdr, "msg #0")));
//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet(&hdr, "msg #1")));

//     REQUIRE_OK(a0_publisher_close(&pub));
//   }
//   {
//     a0_publisher_t pub;
//     REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));

//     a0_packet_header_t hdr = {"key", "val"};

//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet(&hdr, "msg #2")));

//     REQUIRE_OK(a0_publisher_close(&pub));
//   }

//   {
//     a0_subscriber_sync_t sub;
//     REQUIRE_OK(a0_subscriber_sync_init(&sub,
//                                        TEST_TOPIC,
//                                        nullptr,
//                                        a0::test::allocator(),
//                                        A0_INIT_OLDEST,
//                                        A0_ITER_NEXT));

//     uint64_t pkt1_time_mono;

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(has_next);

//       a0_packet_t pkt;
//       REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

//       REQUIRE(pkt.headers_block.size == 5);
//       REQUIRE(pkt.headers_block.next_block == nullptr);

//       std::map<std::string, std::string> hdrs;
//       for (size_t i = 0; i < pkt.headers_block.size; i++) {
//         auto hdr = pkt.headers_block.headers[i];
//         hdrs[hdr.key] = hdr.val;
//       }
//       REQUIRE(hdrs.count("key"));
//       REQUIRE(hdrs.count("a0_time_mono"));
//       REQUIRE(hdrs.count("a0_time_wall"));
//       // REQUIRE(hdrs.count("a0_transport_seq"));
//       REQUIRE(hdrs.count("a0_writer_seq"));
//       REQUIRE(hdrs.count("a0_writer_id"));

//       REQUIRE(a0::test::str(pkt.payload) == "msg #0");

//       REQUIRE(hdrs["key"] == "val");
//       REQUIRE(hdrs["a0_time_mono"].size() < 20);
//       REQUIRE(hdrs["a0_time_wall"].size() == 35);
//       // REQUIRE(hdrs["a0_transport_seq"] == "0");
//       REQUIRE(hdrs["a0_writer_seq"] == "0");
//       REQUIRE(hdrs["a0_writer_id"].size() == 36);
//       pkt1_time_mono = stoull(hdrs["a0_time_mono"]);
//       REQUIRE(pkt1_time_mono > 0);
//       REQUIRE(pkt1_time_mono < UINT64_MAX);
//     }

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(has_next);

//       a0_packet_t pkt;
//       REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

//       std::map<std::string, std::string> hdrs;
//       for (size_t i = 0; i < pkt.headers_block.size; i++) {
//         auto hdr = pkt.headers_block.headers[i];
//         hdrs[hdr.key] = hdr.val;
//       }

//       REQUIRE(a0::test::str(pkt.payload) == "msg #1");

//       REQUIRE(hdrs["key"] == "val");
//       REQUIRE(hdrs["a0_time_mono"].size() < 20);
//       REQUIRE(hdrs["a0_time_wall"].size() == 35);
//       // REQUIRE(hdrs["a0_transport_seq"] == "1");
//       REQUIRE(hdrs["a0_writer_seq"] == "1");
//       REQUIRE(hdrs["a0_writer_id"].size() == 36);

//       uint64_t pkt2_time_mono = stoull(hdrs["a0_time_mono"]);
//       REQUIRE(pkt2_time_mono > pkt1_time_mono);
//       REQUIRE(pkt2_time_mono < UINT64_MAX);
//     }

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(has_next);

//       a0_packet_t pkt;
//       REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

//       std::map<std::string, std::string> hdrs;
//       for (size_t i = 0; i < pkt.headers_block.size; i++) {
//         auto hdr = pkt.headers_block.headers[i];
//         hdrs[hdr.key] = hdr.val;
//       }

//       REQUIRE(a0::test::str(pkt.payload) == "msg #2");

//       REQUIRE(hdrs["key"] == "val");
//       REQUIRE(hdrs["a0_time_mono"].size() < 20);
//       REQUIRE(hdrs["a0_time_wall"].size() == 35);
//       // REQUIRE(hdrs["a0_transport_seq"] == "2");
//       REQUIRE(hdrs["a0_writer_seq"] == "0");
//       REQUIRE(hdrs["a0_writer_id"].size() == 36);
//     }

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(!has_next);
//     }

//     REQUIRE_OK(a0_subscriber_sync_close(&sub));
//   }

//   {
//     a0_subscriber_sync_t sub;
//     REQUIRE_OK(a0_subscriber_sync_init(&sub,
//                                        TEST_TOPIC,
//                                        nullptr,
//                                        a0::test::allocator(),
//                                        A0_INIT_MOST_RECENT,
//                                        A0_ITER_NEWEST));

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(has_next);

//       a0_packet_t pkt;
//       REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));
//       REQUIRE(a0::test::str(pkt.payload) == "msg #2");
//     }

//     {
//       bool has_next;
//       REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//       REQUIRE(!has_next);
//     }

//     REQUIRE_OK(a0_subscriber_sync_close(&sub));
//   }
// }

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] seek immediately await_new") {
//   a0::sync<std::string> msg;

//   a0_packet_callback_t cb = {
//       .user_data = &msg,
//       .fn =
//           [](void* user_data, a0_packet_t pkt) {
//             auto* data = (a0::sync<std::string>*)user_data;
//             data->notify_all([&](auto* msg_) {
//               *msg_ = a0::test::str(pkt.payload);
//             });
//           },
//   };

//   a0_subscriber_t sub;
//   REQUIRE_OK(a0_subscriber_init(&sub,
//                                 file.arena,
//                                 a0::test::allocator(),
//                                 A0_INIT_AWAIT_NEW,
//                                 A0_ITER_NEXT,
//                                 cb));

//   a0_publisher_t pub;
//   REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));
//   REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg")));
//   REQUIRE_OK(a0_publisher_close(&pub));

//   msg.wait([](auto* msg_) {
//     return !msg_->empty();
//   });

//   REQUIRE(msg.copy() == "msg");
//   REQUIRE_OK(a0_subscriber_close(&sub));
// }

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] seek immediately most_recent") {
//   a0::sync<std::string> msg;

//   a0_packet_callback_t cb = {
//       .user_data = &msg,
//       .fn =
//           [](void* user_data, a0_packet_t pkt) {
//             auto* data = (a0::sync<std::string>*)user_data;
//             data->notify_all([&](auto* msg_) {
//               if (msg_->empty()) {
//                 *msg_ = a0::test::str(pkt.payload);
//               }
//             });
//           },
//   };

//   a0_publisher_t pub;
//   REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));
//   REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg before")));

//   a0_subscriber_t sub;
//   REQUIRE_OK(a0_subscriber_init(&sub,
//                                 file.arena,
//                                 a0::test::allocator(),
//                                 A0_INIT_MOST_RECENT,
//                                 A0_ITER_NEXT,
//                                 cb));

//   REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg after")));
//   REQUIRE_OK(a0_publisher_close(&pub));

//   msg.wait([](auto* msg_) {
//     return !msg_->empty();
//   });

//   REQUIRE(msg.copy() == "msg before");
//   REQUIRE_OK(a0_subscriber_close(&sub));
// }

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] multithread") {
//   {
//     a0_publisher_t pub;
//     REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));

//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg #0")));
//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg #1")));

//     REQUIRE_OK(a0_publisher_close(&pub));
//   }

//   a0::sync<size_t> msg_cnt;

//   a0_packet_callback_t cb = {
//       .user_data = &msg_cnt,
//       .fn =
//           [](void* user_data, a0_packet_t pkt) {
//             auto* data = (a0::sync<size_t>*)user_data;
//             data->notify_all([&](auto* msg_cnt_) {
//               if (*msg_cnt_ == 0) {
//                 REQUIRE(a0::test::str(pkt.payload) == "msg #0");
//               } else {
//                 REQUIRE(a0::test::str(pkt.payload) == "msg #1");
//               }

//               (*msg_cnt_)++;
//             });
//           },
//   };

//   a0_subscriber_t sub;
//   REQUIRE_OK(
//       a0_subscriber_init(&sub, file.arena, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT, cb));

//   msg_cnt.wait([](auto* msg_cnt_) {
//     return (*msg_cnt_) == 2;
//   });

//   REQUIRE_OK(a0_subscriber_close(&sub));
// }

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] read one") {
//   // TODO: Blocking, oldest, not available.
//   // TODO: Blocking, most recent, not available.
//   // TODO: Blocking, await new.

//   // Nonblocking, oldest, not available.
//   {
//     a0_packet_t pkt;
//     REQUIRE(
//         a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_OLDEST, O_NONBLOCK, &pkt) ==
//         EAGAIN);
//   }

//   // Nonblocking, most recent, not available.
//   {
//     a0_packet_t pkt;
//     REQUIRE(a0_subscriber_read_one(file.arena,
//                                    a0::test::allocator(),
//                                    A0_INIT_MOST_RECENT,
//                                    O_NONBLOCK,
//                                    &pkt) == EAGAIN);
//   }

//   // Nonblocking, await new.
//   {
//     a0_packet_t pkt;
//     REQUIRE(a0_subscriber_read_one(file.arena,
//                                    a0::test::allocator(),
//                                    A0_INIT_AWAIT_NEW,
//                                    O_NONBLOCK,
//                                    &pkt) == EAGAIN);
//   }

//   // Do writes.
//   {
//     a0_publisher_t pub;
//     REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));

//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg #0")));
//     REQUIRE_OK(a0_publisher_pub(&pub, make_packet("msg #1")));

//     REQUIRE_OK(a0_publisher_close(&pub));
//   }

//   // Blocking, oldest, available.
//   {
//     a0_packet_t pkt;
//     REQUIRE_OK(a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_OLDEST, 0, &pkt));
//     REQUIRE(a0::test::str(pkt.payload) == "msg #0");
//   }

//   // Blocking, most recent, available.
//   {
//     a0_packet_t pkt;
//     REQUIRE_OK(
//         a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_MOST_RECENT, 0, &pkt));
//     REQUIRE(a0::test::str(pkt.payload) == "msg #1");
//   }

//   // Nonblocking, oldest, available.
//   {
//     a0_packet_t pkt;
//     REQUIRE_OK(
//         a0_subscriber_read_one(file.arena, a0::test::allocator(), A0_INIT_OLDEST, O_NONBLOCK, &pkt));
//     REQUIRE(a0::test::str(pkt.payload) == "msg #0");
//   }

//   // Nonblocking, most recent, available.
//   {
//     a0_packet_t pkt;
//     REQUIRE_OK(a0_subscriber_read_one(file.arena,
//                                       a0::test::allocator(),
//                                       A0_INIT_MOST_RECENT,
//                                       O_NONBLOCK,
//                                       &pkt));
//     REQUIRE(a0::test::str(pkt.payload) == "msg #1");
//   }
// }

// TEST_CASE_FIXTURE(PubsubFixture, "pubsub] many publisher fuzz") {
//   constexpr int NUM_THREADS = 10;
//   constexpr int NUM_PACKETS = 500;
//   std::vector<std::thread> threads;

//   for (int i = 0; i < NUM_THREADS; i++) {
//     threads.emplace_back([this, i]() {
//       a0_publisher_t pub;
//       REQUIRE_OK(a0_publisher_init(&pub, TEST_TOPIC, nullptr));

//       for (int j = 0; j < NUM_PACKETS; j++) {
//         REQUIRE_OK(a0_publisher_pub(&pub, make_packet(a0::strutil::fmt("pub %d msg %d", i, j))));
//       }

//       REQUIRE_OK(a0_publisher_close(&pub));
//     });
//   }

//   for (auto&& thread : threads) {
//     thread.join();
//   }

//   // Now sanity-check our values.
//   std::set<std::string> msgs;
//   a0_subscriber_sync_t sub;
//   REQUIRE_OK(
//       a0_subscriber_sync_init(&sub, file.arena, a0::test::allocator(), A0_INIT_OLDEST, A0_ITER_NEXT));

//   while (true) {
//     a0_packet_t pkt;
//     bool has_next = false;

//     REQUIRE_OK(a0_subscriber_sync_has_next(&sub, &has_next));
//     if (!has_next) {
//       break;
//     }
//     REQUIRE_OK(a0_subscriber_sync_next(&sub, &pkt));

//     msgs.insert(a0::test::str(pkt.payload));
//   }

//   REQUIRE_OK(a0_subscriber_sync_close(&sub));

//   // Note that this assumes the topic is lossless.
//   REQUIRE(msgs.size() == 5000);
//   for (int i = 0; i < NUM_THREADS; i++) {
//     for (int j = 0; j < NUM_PACKETS; j++) {
//       REQUIRE(msgs.count(a0::strutil::fmt("pub %d msg %d", i, j)));
//     }
//   }
// }
