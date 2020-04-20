#pragma once

#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/shm.h>

#include <sys/types.h>

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace a0 {

struct Shm {
  std::shared_ptr<a0_shm_t> c;

  struct Options {
    off_t size;
    bool resize;

    static Options DEFAULT;
  };

  Shm() = default;
  Shm(const std::string_view path);
  Shm(const std::string_view path, const Options&);

  std::string path() const;

  static void unlink(const std::string_view path);
};

struct Packet;

struct PacketView {
  std::shared_ptr<a0_packet_t> c;

  PacketView();
  PacketView(const std::string_view payload);
  PacketView(std::vector<std::pair<std::string, std::string>> headers,
             const std::string_view payload);

  PacketView(const Packet&);
  PacketView(a0_packet_t);

  const std::string_view id() const;
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  const std::string_view payload() const;
};

struct Packet {
  std::shared_ptr<a0_packet_t> c;

  Packet();
  Packet(std::string payload);
  Packet(std::vector<std::pair<std::string, std::string>> headers, std::string payload);

  Packet(const PacketView&);
  Packet(PacketView&&);
  Packet(a0_packet_t);

  const std::string_view id() const;
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  const std::string_view payload() const;
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
  Shm log_crit_topic() const;
  Shm log_err_topic() const;
  Shm log_warn_topic() const;
  Shm log_info_topic() const;
  Shm log_dbg_topic() const;
  Shm publisher_topic(const std::string_view) const;
  Shm subscriber_topic(const std::string_view) const;
  Shm rpc_server_topic(const std::string_view) const;
  Shm rpc_client_topic(const std::string_view) const;
  Shm prpc_server_topic(const std::string_view) const;
  Shm prpc_client_topic(const std::string_view) const;
};

void InitGlobalTopicManager(TopicManager);
TopicManager& GlobalTopicManager();

struct PublisherRaw {
  std::shared_ptr<a0_publisher_raw_t> c;

  PublisherRaw() = default;
  PublisherRaw(Shm);
  // User-friendly constructor that uses GlobalTopicManager publisher_topic for shm.
  PublisherRaw(const std::string_view);

  void pub(const PacketView&);
  void pub(std::vector<std::pair<std::string, std::string>> headers,
           const std::string_view payload);
  void pub(const std::string_view payload);
};

struct Publisher {
  std::shared_ptr<a0_publisher_t> c;

  Publisher() = default;
  Publisher(Shm);
  // User-friendly constructor that uses GlobalTopicManager publisher_topic for shm.
  Publisher(const std::string_view);

  void pub(const PacketView&);
  void pub(std::vector<std::pair<std::string, std::string>> headers,
           const std::string_view payload);
  void pub(const std::string_view payload);
};

struct Logger {
  std::shared_ptr<a0_logger_t> c;

  Logger(const TopicManager&);
  Logger();

  void crit(const PacketView&);
  void err(const PacketView&);
  void warn(const PacketView&);
  void info(const PacketView&);
  void dbg(const PacketView&);
};

struct SubscriberSync {
  std::shared_ptr<a0_subscriber_sync_t> c;

  SubscriberSync() = default;
  SubscriberSync(Shm, a0_subscriber_init_t, a0_subscriber_iter_t);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  SubscriberSync(const std::string_view, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  PacketView next();
};

struct Subscriber {
  std::shared_ptr<a0_subscriber_t> c;

  Subscriber() = default;
  Subscriber(Shm,
             a0_subscriber_init_t,
             a0_subscriber_iter_t,
             std::function<void(const PacketView&)>);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  Subscriber(const std::string_view,
             a0_subscriber_init_t,
             a0_subscriber_iter_t,
             std::function<void(const PacketView&)>);
  void async_close(std::function<void()>);

  static Packet read_one(Shm, a0_subscriber_init_t, int flags = 0);
  static Packet read_one(const std::string_view, a0_subscriber_init_t, int flags = 0);
};

Subscriber onconfig(std::function<void(const PacketView&)>);
Packet read_config(int flags = 0);
void write_config(const TopicManager&, const PacketView&);
void write_config(const TopicManager&,
                  std::vector<std::pair<std::string, std::string>> headers,
                  const std::string_view payload);
void write_config(const TopicManager&, const std::string_view payload);

struct RpcServer;

struct RpcRequest {
  std::shared_ptr<a0_rpc_request_t> c;

  RpcServer server();
  PacketView pkt();

  void reply(const PacketView&);
  void reply(std::vector<std::pair<std::string, std::string>> headers,
             const std::string_view payload);
  void reply(const std::string_view payload);
};

struct RpcServer {
  std::shared_ptr<a0_rpc_server_t> c;

  RpcServer() = default;
  RpcServer(Shm,
            std::function<void(RpcRequest)> onrequest,
            std::function<void(const std::string_view)> oncancel);
  // User-friendly constructor that uses GlobalTopicManager rpc_server_topic for shm.
  RpcServer(const std::string_view,
            std::function<void(RpcRequest)> onrequest,
            std::function<void(const std::string_view)> oncancel);
  void async_close(std::function<void()>);
};

struct RpcClient {
  std::shared_ptr<a0_rpc_client_t> c;

  RpcClient() = default;
  RpcClient(Shm);
  // User-friendly constructor that uses GlobalTopicManager rpc_client_topic for shm.
  RpcClient(const std::string_view);
  void async_close(std::function<void()>);

  void send(const PacketView&, std::function<void(const PacketView&)>);
  void send(std::vector<std::pair<std::string, std::string>> headers,
            const std::string_view payload,
            std::function<void(const PacketView&)>);
  void send(const std::string_view payload, std::function<void(const PacketView&)>);
  std::future<Packet> send(const PacketView&);
  std::future<Packet> send(std::vector<std::pair<std::string, std::string>> headers,
                           const std::string_view payload);
  std::future<Packet> send(const std::string_view payload);

  void cancel(const std::string_view);
};

struct PrpcServer;

struct PrpcConnection {
  std::shared_ptr<a0_prpc_connection_t> c;

  PrpcServer server();
  PacketView pkt();

  void send(const PacketView&, bool done);
  void send(std::vector<std::pair<std::string, std::string>> headers,
            const std::string_view payload,
            bool done);
  void send(const std::string_view payload, bool done);
};

struct PrpcServer {
  std::shared_ptr<a0_prpc_server_t> c;

  PrpcServer() = default;
  PrpcServer(Shm,
             std::function<void(PrpcConnection)> onconnect,
             std::function<void(const std::string_view)> oncancel);
  // User-friendly constructor that uses GlobalTopicManager prpc_server_topic for shm.
  PrpcServer(const std::string_view,
             std::function<void(PrpcConnection)> onconnect,
             std::function<void(const std::string_view)> oncancel);
  void async_close(std::function<void()>);
};

struct PrpcClient {
  std::shared_ptr<a0_prpc_client_t> c;

  PrpcClient() = default;
  PrpcClient(Shm);
  // User-friendly constructor that uses GlobalTopicManager prpc_client_topic for shm.
  PrpcClient(const std::string_view);
  void async_close(std::function<void()>);

  void connect(const PacketView&, std::function<void(const PacketView&, bool)>);
  void connect(std::vector<std::pair<std::string, std::string>> headers,
               const std::string_view payload,
               std::function<void(const PacketView&, bool)>);
  void connect(const std::string_view payload, std::function<void(const PacketView&, bool)>);

  void cancel(const std::string_view);
};

}  // namespace a0
