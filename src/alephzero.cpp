#include <a0/alephzero.hpp>
// #include <a0/alloc.h>
// #include <a0/arena.h>
// #include <a0/buf.h>
// #include <a0/callback.h>
// #include <a0/err.h>
// #include <a0/file.h>
// #include <a0/heartbeat.h>
// #include <a0/logger.h>
// #include <a0/packet.h>
// #include <a0/prpc.h>
// #include <a0/pubsub.h>
// #include <a0/rpc.h>
// #include <a0/topic_manager.h>
// #include <a0/uuid.h>

// #include <cerrno>
// #include <chrono>
// #include <cstdint>
// #include <cstdio>
// #include <cstring>
// #include <exception>
// #include <functional>
// #include <memory>
// #include <stdexcept>
// #include <string>
// #include <string_view>
// #include <system_error>
// #include <thread>
// #include <type_traits>
// #include <utility>
// #include <vector>

// #include "alloc_util.hpp"
// #include "inline.h"
// #include "scope.hpp"
// #include "strutil.hpp"

namespace a0 {

// Subscriber onconfig(std::function<void(const PacketView&)> fn) {
//   return Subscriber(GlobalTopicManager().config_topic(),
//                     A0_INIT_MOST_RECENT,
//                     A0_ITER_NEWEST,
//                     std::move(fn));
// }

// Packet read_config(int flags) {
//   return Subscriber::read_one(GlobalTopicManager().config_topic(), A0_INIT_MOST_RECENT, flags);
// }

// // void write_config(const TopicManager& tm, const PacketView& pkt) {
// //   Publisher p(tm.config_topic());
// //   p.pub(pkt);
// // }

// void write_config(const TopicManager& tm,
//                   std::vector<std::pair<std::string, std::string>> headers,
//                   a0::string_view payload) {
//   write_config(tm, PacketView(std::move(headers), payload));
// }

// void write_config(const TopicManager& tm, a0::string_view payload) {
//   write_config(tm, {}, payload);
// }

// Logger::Logger(const TopicManager& topic_manager) {
//   auto shm_crit = topic_manager.log_crit_topic();
//   auto shm_err = topic_manager.log_err_topic();
//   auto shm_warn = topic_manager.log_warn_topic();
//   auto shm_info = topic_manager.log_info_topic();
//   auto shm_dbg = topic_manager.log_dbg_topic();
//   set_c(
//       &c,
//       [&](a0_logger_t* c) {
//         return a0_logger_init(c,
//                               shm_crit.c->arena,
//                               shm_err.c->arena,
//                               shm_warn.c->arena,
//                               shm_info.c->arena,
//                               shm_dbg.c->arena);
//       },
//       [shm_crit, shm_err, shm_warn, shm_info, shm_dbg](a0_logger_t* c) {
//         a0_logger_close(c);
//       });
// }

// Logger::Logger()
//     : Logger(GlobalTopicManager()) {}

// void Logger::crit(const PacketView& pkt) {
//   CHECK_C;
//   check(a0_log_crit(&*c, *pkt.c));
// }
// void Logger::err(const PacketView& pkt) {
//   CHECK_C;
//   check(a0_log_err(&*c, *pkt.c));
// }
// void Logger::warn(const PacketView& pkt) {
//   CHECK_C;
//   check(a0_log_warn(&*c, *pkt.c));
// }
// void Logger::info(const PacketView& pkt) {
//   CHECK_C;
//   check(a0_log_info(&*c, *pkt.c));
// }
// void Logger::dbg(const PacketView& pkt) {
//   CHECK_C;
//   check(a0_log_dbg(&*c, *pkt.c));
// }

// Heartbeat::Options Heartbeat::Options::DEFAULT = {
//     .freq = A0_HEARTBEAT_OPTIONS_DEFAULT.freq,
// };

// Heartbeat::Heartbeat(Arena arena, Options opts) {
//   a0_heartbeat_options_t c_opts{
//       .freq = opts.freq,
//   };
//   set_c(
//       &c,
//       [&](a0_heartbeat_t* c) {
//         return a0_heartbeat_init(c, *arena.c, &c_opts);
//       },
//       [arena](a0_heartbeat_t* c) {
//         a0_heartbeat_close(c);
//       });
// }

// Heartbeat::Heartbeat(Arena arena)
//     : Heartbeat(std::move(arena), Options::DEFAULT) {}

// Heartbeat::Heartbeat(Options opts)
//     : Heartbeat(GlobalTopicManager().heartbeat_topic(), opts) {}

// Heartbeat::Heartbeat()
//     : Heartbeat(Options::DEFAULT) {}

// HeartbeatListener::Options HeartbeatListener::Options::DEFAULT = {
//     .min_freq = A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT.min_freq,
// };

// HeartbeatListener::HeartbeatListener(Arena arena,
//                                      Options opts,
//                                      std::function<void()> ondetected,
//                                      std::function<void()> onmissed) {
//   a0_heartbeat_listener_options_t c_opts{
//       .min_freq = opts.min_freq,
//   };

//   CDeleter<a0_heartbeat_listener_t> deleter;
//   deleter.also.emplace_back([arena]() {});

//   struct data_t {
//     std::function<void()> ondetected;
//     std::function<void()> onmissed;
//   };
//   auto* heap_data = new data_t{std::move(ondetected), std::move(onmissed)};

//   a0_callback_t c_ondetected = {
//       .user_data = heap_data,
//       .fn =
//           [](void* user_data) {
//             if ((*(data_t*)user_data).ondetected) {
//               (*(data_t*)user_data).ondetected();
//             }
//           },
//   };
//   a0_callback_t c_onmissed = {
//       .user_data = heap_data,
//       .fn =
//           [](void* user_data) {
//             if ((*(data_t*)user_data).onmissed) {
//               (*(data_t*)user_data).onmissed();
//             }
//           },
//   };
//   deleter.also.emplace_back([heap_data]() {
//     delete heap_data;
//   });

//   a0_alloc_t alloc;
//   check(a0_realloc_allocator_init(&alloc));
//   deleter.also.emplace_back([alloc]() mutable {
//     a0_realloc_allocator_close(&alloc);
//   });

//   deleter.primary = a0_heartbeat_listener_close;

//   set_c(
//       &c,
//       [&](a0_heartbeat_listener_t* c) {
//         return a0_heartbeat_listener_init(c, *arena.c, alloc, &c_opts, c_ondetected, c_onmissed);
//       },
//       std::move(deleter));
// }

// HeartbeatListener::HeartbeatListener(Arena arena,
//                                      std::function<void()> ondetected,
//                                      std::function<void()> onmissed)
//     : HeartbeatListener(std::move(arena), Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}
// HeartbeatListener::HeartbeatListener(a0::string_view container,
//                                      Options opts,
//                                      std::function<void()> ondetected,
//                                      std::function<void()> onmissed)
//     : HeartbeatListener(
//           TopicManager{
//               .container = std::string(container),
//               .subscriber_aliases = {},
//               .rpc_client_aliases = {},
//               .prpc_client_aliases = {},
//           }
//               .heartbeat_topic(),
//           opts,
//           std::move(ondetected),
//           std::move(onmissed)) {}
// HeartbeatListener::HeartbeatListener(a0::string_view container,
//                                      std::function<void()> ondetected,
//                                      std::function<void()> onmissed)
//     : HeartbeatListener(container, Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}
// HeartbeatListener::HeartbeatListener(Options opts,
//                                      std::function<void()> ondetected,
//                                      std::function<void()> onmissed)
//     : HeartbeatListener(GlobalTopicManager().container, opts, std::move(ondetected), std::move(onmissed)) {}
// HeartbeatListener::HeartbeatListener(std::function<void()> ondetected,
//                                      std::function<void()> onmissed)
//     : HeartbeatListener(Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}

// void HeartbeatListener::async_close(std::function<void()> fn) {
//   if (!c) {
//     TRY("a0::HeartbeatListener::async_close callback", fn());
//     return;
//   }
//   auto* deleter = std::get_deleter<CDeleter<a0_heartbeat_listener_t>>(c);
//   deleter->primary = nullptr;

//   struct data_t {
//     std::shared_ptr<a0_heartbeat_listener_t> c;
//     std::function<void()> fn;
//   };
//   auto* heap_data = new data_t{c, std::move(fn)};
//   a0_callback_t callback = {
//       .user_data = heap_data,
//       .fn =
//           [](void* user_data) {
//             auto* data = (data_t*)user_data;
//             TRY("a0::HeartbeatListener::async_close callback", data->fn());
//             delete data;
//           },
//   };

//   check(a0_heartbeat_listener_async_close(&*heap_data->c, callback));
// }

}  // namespace a0
