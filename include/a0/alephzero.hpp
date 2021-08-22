#pragma once

#include <a0/arena.hpp>
#include <a0/buf.hpp>
#include <a0/file.hpp>
#include <a0/middleware.hpp>
#include <a0/packet.hpp>
#include <a0/prpc.hpp>
#include <a0/pubsub.hpp>
#include <a0/reader.hpp>
#include <a0/rpc.hpp>
#include <a0/time.hpp>
#include <a0/transport.hpp>
#include <a0/writer.hpp>

namespace a0 {

// Subscriber onconfig(std::function<void(const PacketView&)>);
// Packet read_config(int flags = 0);
// void write_config(const TopicManager&, const PacketView&);
// void write_config(const TopicManager&,
//                   std::vector<std::pair<std::string, std::string>> headers,
//                   std::string_view payload);
// void write_config(const TopicManager&, std::string_view payload);

// struct Logger : details::CppWrap<a0_logger_t> {
//   Logger(const TopicManager&);
//   Logger();

//   void crit(const PacketView&);
//   void err(const PacketView&);
//   void warn(const PacketView&);
//   void info(const PacketView&);
//   void dbg(const PacketView&);
// };

// struct Heartbeat : details::CppWrap<a0_heartbeat_t> {
//   struct Options {
//     /// Frequency at which heartbeat packets will be published.
//     double freq;

//     /// The default options to be used if no options are explicitly provided.
//     /// Default freq is 10Hz.
//     static Options DEFAULT;
//   };

//   /// Primary constructor.
//   Heartbeat(Arena, Options);
//   /// User-friendly constructor.
//   /// * Uses default options.
//   Heartbeat(Arena);
//   /// User-friendly constructor.
//   /// * Uses GlobalTopicManager heartbeat_topic to get the shm.
//   Heartbeat(Options);
//   /// User-friendly constructor.
//   /// * Uses default options.
//   /// * Uses GlobalTopicManager heartbeat_topic to get the shm.
//   Heartbeat();
// };

// struct HeartbeatListener : details::CppWrap<a0_heartbeat_listener_t> {
//   struct Options {
//     /// Frequency at which heartbeat packets will be checked.
//     /// This should be less than the frequency at which the associated Heartbeat publishes.
//     double min_freq;

//     /// The default options to be used if no options are explicitly provided.
//     /// Default freq is 5Hz.
//     static Options DEFAULT;
//   };

//   HeartbeatListener() = default;

//   /// Primary constructor.
//   /// ondetected will be executed once, when the first heartbeat packet is read.
//   /// onmissed will be executed once, after ondetected, when a period of time passes,
//   ///          defined by min_freq, without a heartbeat packet is read.
//   HeartbeatListener(Arena,
//                     Options,
//                     std::function<void()> ondetected,
//                     std::function<void()> onmissed);
//   /// User-friendly constructor.
//   /// * Uses default options.
//   HeartbeatListener(Arena,
//                     std::function<void()> ondetected,
//                     std::function<void()> onmissed);
//   /// User-friendly constructor.
//   /// * Constructs the Shm from the target container name.
//   HeartbeatListener(std::string_view container,
//                     Options,
//                     std::function<void()> ondetected,
//                     std::function<void()> onmissed);
//   /// User-friendly constructor.
//   /// * Uses default options.
//   /// * Constructs the Shm from the target container name.
//   HeartbeatListener(std::string_view container,
//                     std::function<void()> ondetected,
//                     std::function<void()> onmissed);
//   /// User-friendly constructor.
//   /// * Constructs the Shm from the current container, using GlobalTopicManager.
//   HeartbeatListener(Options,
//                     std::function<void()> ondetected,
//                     std::function<void()> onmissed);
//   /// User-friendly constructor.
//   /// * Uses default options.
//   /// * Constructs the Shm from the current container, using GlobalTopicManager.
//   HeartbeatListener(std::function<void()> ondetected,
//                     std::function<void()> onmissed);

//   /// Closes HeartbeatListener.
//   /// Unlike the destructor, this can be called from within a callback.
//   void async_close(std::function<void()>);
// };

}  // namespace a0
