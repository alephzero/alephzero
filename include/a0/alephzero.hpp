#include <a0/packet.h>         // for a0_packet_t
#include <a0/pubsub.h>         // for a0_subscriber_init_t, a0_subscriber_it...
#include <a0/rpc.h>            // for a0_rpc_client_t, a0_rpc_request_t, a0_...
#include <a0/shm.h>            // for a0_shm_t
#include <a0/topic_manager.h>  // for a0_topic_manager_t
#include <stddef.h>            // for size_t
#include <stdint.h>            // for uint8_t
#include <sys/types.h>         // for off_t
#include <functional>          // for function
#include <memory>              // for shared_ptr
#include <string>              // for string
#include <string_view>         // for string_view
#include <utility>             // for pair
#include <vector>              // for vector

namespace a0 {

struct Shm {
  std::shared_ptr<a0_shm_t> c;

  struct Options {
    off_t size;
  };

  Shm() = default;
  Shm(const std::string& path);
  Shm(const std::string& path, const Options&);

  std::string path() const;

  static void unlink(const std::string& path);
};

struct PacketView {
  a0_packet_t c;

  size_t num_headers() const;
  std::pair<std::string_view, std::string_view> header(size_t idx) const;
  std::string_view payload() const;

  std::string id() const;
};

struct Packet {
  std::vector<uint8_t> mem;
  const a0_packet_t c() const;

  Packet() = default;
  Packet(PacketView);
  Packet(std::string_view payload);
  Packet(const std::vector<std::pair<std::string_view, std::string_view>>& hdrs,
         std::string_view payload);

  size_t num_headers() const;
  std::pair<std::string_view, std::string_view> header(size_t idx) const;
  std::string_view payload() const;

  std::string id() const;
};

struct TopicManager {
  std::shared_ptr<a0_topic_manager_t> c;

  TopicManager() = default;
  TopicManager(const std::string& json);

  Shm config_topic();
  Shm publisher_topic(const std::string&);
  Shm subscriber_topic(const std::string&);
  Shm rpc_server_topic(const std::string&);
  Shm rpc_client_topic(const std::string&);
};

void InitGlobalTopicManager(TopicManager);
void InitGlobalTopicManager(const std::string& json);

struct Publisher {
  std::shared_ptr<a0_publisher_t> c;

  Publisher() = default;
  Publisher(Shm);
  // User-friendly constructor that uses GlobalTopicManager publisher_topic for shm.
  Publisher(const std::string&);

  void pub(const Packet&);
  void pub(std::string_view);
};

struct SubscriberSync {
  std::shared_ptr<a0_subscriber_sync_t> c;

  SubscriberSync() = default;
  SubscriberSync(Shm, a0_subscriber_init_t, a0_subscriber_iter_t);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  SubscriberSync(const std::string&, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  PacketView next();
};

struct Subscriber {
  std::shared_ptr<a0_subscriber_t> c;

  Subscriber() = default;
  Subscriber(Shm, a0_subscriber_init_t, a0_subscriber_iter_t, std::function<void(PacketView)>);
  // User-friendly constructor that uses GlobalTopicManager subscriber_topic for shm.
  Subscriber(const std::string&, a0_subscriber_init_t, a0_subscriber_iter_t, std::function<void(PacketView)>);
  void async_close(std::function<void()>);

  static Packet read_one(Shm, a0_subscriber_init_t, int flags = 0);
  static Packet read_one(const std::string&, a0_subscriber_init_t, int flags = 0);
};

Packet read_config(int flags = 0);

struct RpcServer;

struct RpcRequest {
  std::shared_ptr<a0_rpc_request_t> c;

  RpcServer server();
  PacketView pkt();

  void reply(const Packet&);
  void reply(std::string_view);
};

struct RpcServer {
  std::shared_ptr<a0_rpc_server_t> c;

  RpcServer() = default;
  RpcServer(Shm, std::function<void(RpcRequest)> onrequest, std::function<void(std::string)> oncancel);
  // User-friendly constructor that uses GlobalTopicManager rpc_server_topic for shm.
  RpcServer(const std::string&, std::function<void(RpcRequest)> onrequest, std::function<void(std::string)> oncancel);
  void async_close(std::function<void()>);
};

struct RpcClient {
  std::shared_ptr<a0_rpc_client_t> c;

  RpcClient() = default;
  RpcClient(Shm);
  // User-friendly constructor that uses GlobalTopicManager rpc_client_topic for shm.
  RpcClient(const std::string&);
  void async_close(std::function<void()>);

  void send(const Packet&, std::function<void(PacketView)>);
  void send(std::string_view, std::function<void(PacketView)>);
  void cancel(const std::string&);
};

}  // namespace a0;
