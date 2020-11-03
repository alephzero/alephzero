#pragma once

#include <a0/arena.h>
#include <a0/heartbeat.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>

#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
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

  CppWrap()
      : magic_number{0xA0A0A0A0} {}
  ~CppWrap() {
    magic_number = 0xDEADBEEF;
  }
};

}  // namespace details

struct Arena : details::CppWrap<a0_arena_t> {
  size_t size() const;
};

struct File : details::CppWrap<a0_file_t> {
  /// Options for creating new files or directories.
  ///
  /// These will not change existing files.
  struct Options {
    struct CreateOptions {
      /// File size.
      off_t size;
      /// File mode.
      mode_t mode;
      /// Mode for directories that will be created as part of file creation.
      mode_t dir_mode;
    } create_options;

    struct OpenOptions {
      /// Create a private copy-on-write mapping.
      /// Updates to the mapping are not visible to other processes mapping
      /// the same file, and are not carried through to the underlying file.
      /// It is unspecified whether changes made to the file are visible in
      /// the mapped region.
      bool readonly;
    } open_options;

    /// Default file creation options.
    ///
    /// 16MB and universal read+write.
    static Options DEFAULT;
  };

  File() = default;
  File(std::string_view path);
  File(std::string_view path, Options);

  /// Implicit conversion to Arena.
  operator Arena() const;

  /// File size.
  size_t size() const;
  /// File path.
  std::string path() const;

  /// File descriptor.
  int fd() const;
  /// File state.
  stat_t stat() const;

  /// Removes the specified file.
  static void remove(std::string_view path);
  /// Removes the specified file or directory, including all subdirectories.
  static void remove_all(std::string_view path);
};

struct Packet;

/// PacketView does not own the underlying data.
///
/// Ownership and lifetime semantics of payload is managed externally.
///
/// PacketView is immutable.
struct PacketView : details::CppWrap<a0_packet_t> {
  /// Creates a new packet view with no headers and an empty payload.
  PacketView();
  /// Creates a new packet view with no headers and the given payload.
  PacketView(std::string_view payload);
  /// Creates a new packet view with the given headers and the given payload.
  PacketView(std::vector<std::pair<std::string, std::string>> headers,
             std::string_view payload);

  /// Create a shallow copy of the given Packet.
  PacketView(const Packet&);
  PacketView(a0_packet_t);

  /// Packet unique identifier.
  std::string_view id() const;
  /// Packet headers.
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  /// Packet payload.
  std::string_view payload() const;
};

/// Packet owns the underlying data.
///
/// Packet is immutable.
struct Packet : details::CppWrap<a0_packet_t> {
  /// Creates a new packet with no headers and an empty payload.
  Packet();
  /// Creates a new packet with no headers and the given payload.
  Packet(std::string payload);
  /// Creates a new packet with the given headers and the given payload.
  Packet(std::vector<std::pair<std::string, std::string>> headers,
         std::string payload);

  /// Create a deep copy of the given PacketView.
  Packet(const PacketView&);
  /// Create a deep copy of the given PacketView.
  Packet(PacketView&&);
  Packet(a0_packet_t);

  /// Packet unique identifier.
  std::string_view id() const;
  /// Packet headers.
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  /// Packet payload.
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

  File config_topic() const;
  File heartbeat_topic() const;
  File log_crit_topic() const;
  File log_err_topic() const;
  File log_warn_topic() const;
  File log_info_topic() const;
  File log_dbg_topic() const;
  File publisher_topic(std::string_view) const;
  File subscriber_topic(std::string_view) const;
  File rpc_server_topic(std::string_view) const;
  File rpc_client_topic(std::string_view) const;
  File prpc_server_topic(std::string_view) const;
  File prpc_client_topic(std::string_view) const;
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
