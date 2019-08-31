#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/shmobj.h>
#include <a0/stream.h>
#include <a0/topic_manager.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace a0 {

struct ShmObj {
  std::shared_ptr<a0_shmobj_t> c;

  struct Options {
    off_t size;
  };

  ShmObj() = default;
  ShmObj(const std::string& path);
  ShmObj(const std::string& path, const Options&);

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

struct Publisher {
  std::shared_ptr<a0_publisher_t> c;

  Publisher() = default;
  Publisher(ShmObj);

  void pub(const Packet&);
  void pub(std::string_view);
};

struct SubscriberSync {
  std::shared_ptr<a0_subscriber_sync_t> c;

  SubscriberSync() = default;
  SubscriberSync(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  PacketView next();
};

struct Subscriber {
  std::shared_ptr<a0_subscriber_t> c;

  Subscriber() = default;
  Subscriber(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t, std::function<void(PacketView)>);
  void async_close(std::function<void()>);
};

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
  RpcServer(ShmObj, std::function<void(RpcRequest)> onrequest, std::function<void(std::string)> oncancel);
  void async_close(std::function<void()>);
};

struct RpcClient {
  std::shared_ptr<a0_rpc_client_t> c;

  RpcClient() = default;
  RpcClient(ShmObj);
  void async_close(std::function<void()>);

  void send(const Packet&, std::function<void(PacketView)>);
  void send(std::string_view, std::function<void(PacketView)>);
  void cancel(const std::string&);
};

struct TopicManager {
  std::shared_ptr<a0_topic_manager_t> c;

  TopicManager() = default;
  // TODO: TopicManager(const Options&);
  TopicManager(const std::string& json);

  ShmObj config_topic();
  ShmObj publisher_topic(const std::string&);
  ShmObj subscriber_topic(const std::string&);
  ShmObj rpc_server_topic(const std::string&);
  ShmObj rpc_client_topic(const std::string&);
};

}  // namespace a0;
