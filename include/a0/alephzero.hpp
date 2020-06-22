#pragma once

#include <a0/common.h>
#include <a0/file_arena.h>
#include <a0/heartbeat.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>

#include <sys/types.h>

#include <cstddef>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace a0 {

namespace details {

template <typename CType>
struct CppWrap {
  std::shared_ptr<CType> c;
  uint32_t magic_number;

  CppWrap() : magic_number{0xA0A0A0A0} {}
  ~CppWrap() {
    magic_number = 0xDEADBEEF;
  }
};

}  // namespace details

struct Arena : details::CppWrap<a0_arena_t> {
  size_t size() const;
};

struct Disk : details::CppWrap<a0_disk_t> {
  struct Options {
    off_t size;
    bool resize;

    static Options DEFAULT;
  };

  Disk() = default;
  Disk(std::string_view path);
  Disk(std::string_view path, Options);

  operator Arena() const;

  size_t size() const;
  std::string path() const;

  static void unlink(std::string_view path);
};

struct Shm : details::CppWrap<a0_shm_t> {
  struct Options {
    off_t size;
    bool resize;

    static Options DEFAULT;
  };

  Shm() = default;
  Shm(std::string_view path);
  Shm(std::string_view path, Options);

  operator Arena() const;

  size_t size() const;
  std::string path() const;

  static void unlink(std::string_view path);
};

struct Packet;

struct PacketView : details::CppWrap<a0_packet_t> {
  PacketView();
  PacketView(std::string_view payload);
  PacketView(std::vector<std::pair<std::string, std::string>> headers,
             std::string_view payload);

  PacketView(const Packet&);
  PacketView(a0_packet_t);

  std::string_view id() const;
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  std::string_view payload() const;
};

struct Packet : details::CppWrap<a0_packet_t> {
  Packet();
  Packet(std::string payload);
  Packet(std::vector<std::pair<std::string, std::string>> headers,
         std::string payload);

  Packet(const PacketView&);
  Packet(PacketView&&);
  Packet(a0_packet_t);

  std::string_view id() const;
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  std::string_view payload() const;
};

struct TopicAliasTarget {
  std::string container;
  std::string topic;
};

struct TopicManager {
  std::string container;

  std::map<std::string, TopicAliasTarget> subscriber_aliases;
  std::map<std::string, TopicAliasTarget> rpc_client_aliases;
  std::map<std::string, TopicAliasTarget> prpc_client_aliases;

  Shm config_topic() const;
  Shm heartbeat_topic() const;
  Shm log_crit_topic() const;
  Shm log_err_topic() const;
  Shm log_warn_topic() const;
  Shm log_info_topic() const;
  Shm log_dbg_topic() const;
  Shm publisher_topic(std::string_view) const;
  Shm subscriber_topic(std::string_view) const;
  Shm rpc_server_topic(std::string_view) const;
  Shm rpc_client_topic(std::string_view) const;
  Shm prpc_server_topic(std::string_view) const;
  Shm prpc_client_topic(std::string_view) const;
};

void InitGlobalTopicManager(TopicManager);
TopicManager& GlobalTopicManager();

struct PublisherRaw : details::CppWrap<a0_publisher_raw_t> {
  PublisherRaw() = default;
  PublisherRaw(Arena);
  // User-friendly constructor that uses GlobalTopicManager publisher_topic for shm.
  PublisherRaw(std::string_view);

  void pub(const PacketView&);
  void pub(std::vector<std::pair<std::string, std::string>> headers,
           std::string_view payload);
  void pub(std::string_view payload);
};

struct Publisher : details::CppWrap<a0_publisher_t> {
  Publisher() = default;
  Publisher(Arena);
  // User-friendly constructor that uses GlobalTopicManager publisher_topic for shm.
  Publisher(std::string_view);

  void pub(const PacketView&);
  void pub(std::vector<std::pair<std::string, std::string>> headers,
           std::string_view payload);
  void pub(std::string_view payload);
};

struct SubscriberSync : details::CppWrap<a0_subscriber_sync_t> {
  SubscriberSync() = default;
  SubscriberSync(Arena, a0_subscriber_init_t, a0_subscriber_iter_t);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  SubscriberSync(std::string_view, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  PacketView next();
};

struct Subscriber : details::CppWrap<a0_subscriber_t> {
  Subscriber() = default;
  Subscriber(Arena,
             a0_subscriber_init_t,
             a0_subscriber_iter_t,
             std::function<void(const PacketView&)>);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  Subscriber(std::string_view,
             a0_subscriber_init_t,
             a0_subscriber_iter_t,
             std::function<void(const PacketView&)>);
  void async_close(std::function<void()>);

  static Packet read_one(Arena, a0_subscriber_init_t, int flags = 0);
  static Packet read_one(std::string_view, a0_subscriber_init_t, int flags = 0);
};

Subscriber onconfig(std::function<void(const PacketView&)>);
Packet read_config(int flags = 0);
void write_config(const TopicManager&, const PacketView&);
void write_config(const TopicManager&,
                  std::vector<std::pair<std::string, std::string>> headers,
                  std::string_view payload);
void write_config(const TopicManager&, std::string_view payload);

struct RpcServer;

struct RpcRequest : details::CppWrap<a0_rpc_request_t> {
  RpcServer server();
  PacketView pkt();

  void reply(const PacketView&);
  void reply(std::vector<std::pair<std::string, std::string>> headers,
             std::string_view payload);
  void reply(std::string_view payload);
};

struct RpcServer : details::CppWrap<a0_rpc_server_t> {
  RpcServer() = default;
  RpcServer(Arena,
            std::function<void(RpcRequest)> onrequest,
            std::function<void(std::string_view)> oncancel);
  // User-friendly constructor that uses GlobalTopicManager rpc_server_topic for shm.
  RpcServer(std::string_view,
            std::function<void(RpcRequest)> onrequest,
            std::function<void(std::string_view)> oncancel);
  void async_close(std::function<void()>);
};

struct RpcClient : details::CppWrap<a0_rpc_client_t> {
  RpcClient() = default;
  RpcClient(Arena);
  // User-friendly constructor that uses GlobalTopicManager rpc_client_topic for shm.
  RpcClient(std::string_view);
  void async_close(std::function<void()>);

  void send(const PacketView&, std::function<void(const PacketView&)>);
  void send(std::vector<std::pair<std::string, std::string>> headers,
            std::string_view payload,
            std::function<void(const PacketView&)>);
  void send(std::string_view payload, std::function<void(const PacketView&)>);
  std::future<Packet> send(const PacketView&);
  std::future<Packet> send(std::vector<std::pair<std::string, std::string>> headers,
                           std::string_view payload);
  std::future<Packet> send(std::string_view payload);

  void cancel(std::string_view);
};

struct PrpcServer;

struct PrpcConnection : details::CppWrap<a0_prpc_connection_t> {
  PrpcServer server();
  PacketView pkt();

  void send(const PacketView&, bool done);
  void send(std::vector<std::pair<std::string, std::string>> headers,
            std::string_view payload,
            bool done);
  void send(std::string_view payload, bool done);
};

struct PrpcServer : details::CppWrap<a0_prpc_server_t> {
  PrpcServer() = default;
  PrpcServer(Arena,
             std::function<void(PrpcConnection)> onconnect,
             std::function<void(std::string_view)> oncancel);
  // User-friendly constructor that uses GlobalTopicManager prpc_server_topic for shm.
  PrpcServer(std::string_view,
             std::function<void(PrpcConnection)> onconnect,
             std::function<void(std::string_view)> oncancel);
  void async_close(std::function<void()>);
};

struct PrpcClient : details::CppWrap<a0_prpc_client_t> {
  PrpcClient() = default;
  PrpcClient(Arena);
  // User-friendly constructor that uses GlobalTopicManager prpc_client_topic for shm.
  PrpcClient(std::string_view);
  void async_close(std::function<void()>);

  void connect(const PacketView&,
               std::function<void(const PacketView&, bool)>);
  void connect(std::vector<std::pair<std::string, std::string>> headers,
               std::string_view payload,
               std::function<void(const PacketView&, bool)>);
  void connect(std::string_view payload,
               std::function<void(const PacketView&, bool)>);

  void cancel(std::string_view);
};

struct Logger : details::CppWrap<a0_logger_t> {
  Logger(const TopicManager&);
  Logger();

  void crit(const PacketView&);
  void err(const PacketView&);
  void warn(const PacketView&);
  void info(const PacketView&);
  void dbg(const PacketView&);
};

struct Heartbeat : details::CppWrap<a0_heartbeat_t> {
  struct Options {
    /// Frequency at which heartbeat packets will be published.
    double freq;

    /// The default options to be used if no options are explicitly provided.
    /// Default freq is 10Hz.
    static Options DEFAULT;
  };

  /// Primary constructor.
  Heartbeat(Arena, Options);
  /// User-friendly constructor.
  /// * Uses default options.
  Heartbeat(Arena);
  /// User-friendly constructor.
  /// * Uses GlobalTopicManager heartbeat_topic to get the shm.
  Heartbeat(Options);
  /// User-friendly constructor.
  /// * Uses default options.
  /// * Uses GlobalTopicManager heartbeat_topic to get the shm.
  Heartbeat();
};

struct HeartbeatListener : details::CppWrap<a0_heartbeat_listener_t> {
  struct Options {
    /// Frequency at which heartbeat packets will be checked.
    /// This should be less than the frequency at which the associated Heartbeat publishes.
    double min_freq;

    /// The default options to be used if no options are explicitly provided.
    /// Default freq is 5Hz.
    static Options DEFAULT;
  };

  HeartbeatListener() = default;

  /// Primary constructor.
  /// ondetected will be executed once, when the first heartbeat packet is read.
  /// onmissed will be executed once, after ondetected, when a period of time passes,
  ///          defined by min_freq, without a heartbeat packet is read.
  HeartbeatListener(Arena,
                    Options,
                    std::function<void()> ondetected,
                    std::function<void()> onmissed);
  /// User-friendly constructor.
  /// * Uses default options.
  HeartbeatListener(Arena,
                    std::function<void()> ondetected,
                    std::function<void()> onmissed);
  /// User-friendly constructor.
  /// * Constructs the Shm from the target container name.
  HeartbeatListener(std::string_view container,
                    Options,
                    std::function<void()> ondetected,
                    std::function<void()> onmissed);
  /// User-friendly constructor.
  /// * Uses default options.
  /// * Constructs the Shm from the target container name.
  HeartbeatListener(std::string_view container,
                    std::function<void()> ondetected,
                    std::function<void()> onmissed);
  /// User-friendly constructor.
  /// * Constructs the Shm from the current container, using GlobalTopicManager.
  HeartbeatListener(Options,
                    std::function<void()> ondetected,
                    std::function<void()> onmissed);
  /// User-friendly constructor.
  /// * Uses default options.
  /// * Constructs the Shm from the current container, using GlobalTopicManager.
  HeartbeatListener(std::function<void()> ondetected,
                    std::function<void()> onmissed);

  /// Closes HeartbeatListener.
  /// Unlike the destructor, this can be called from within a callback.
  void async_close(std::function<void()>);
};

}  // namespace a0
