#include <alephzero.h>

namespace a0 {

class ShmObj {
 public:
  std::shared_ptr<a0_shmobj_t> c;

  struct Options {};

  ShmObj(const std::string& path);
  ShmObj(const std::string& path, const Options&);

  static void unlink(const std::string& path);
};

class TopicManager {
 public:
  std::shared_ptr<a0_topic_manager_t> c;

  struct FullTopic {
    std::string container;
    std::string topic;
  };
  struct Options {
    std::string container;

    std::unordered_map<std::string, FullTopic> subscriber_maps;
    std::unordered_map<std::string, FullTopic> rpc_client_maps;
  };

  TopicManager(const Options&);
  TopicManager(const std::string& json);

  ShmObj config_topic();
  ShmObj publisher_topic(const std::string&);
  ShmObj subscriber_topic(const std::string&);
  ShmObj rpc_server_topic(const std::string&);
  ShmObj rpc_client_topic(const std::string&);
};

class Packet {
  std::string mem;

 public:
  static Packet build(const std::vector<std::pair<std::string, std::string>>& hdrs,
                      const std::string& payload);

  size_t num_headers();
  std::pair<std::string, std::string> header(size_t idx);
  std::string payload();

  std::unique_ptr<std::string> find_header(const std::string& key);
  std::string id();
};

class Publisher {
 public:
  std::shared_ptr<a0_publisher_t> c;

  Publisher(ShmObj);

  void pub(const Packet&);
};

class SubscriberSync {
 public:
  std::shared_ptr<a0_subscriber_sync_t> c;

  SubscriberSync(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  Packet next();
};

class Subscriber {
 public:
  std::shared_ptr<a0_subscriber_t> c;

  Subscriber(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t, std::function<void(Packet)>);
  void async_close(std::function<void()>);
};

class RpcServer {
 public:
  std::shared_ptr<a0_rpc_server_t> c;

  RpcServer(ShmObj, std::function<void(Packet)> onrequest, std::function<void(a0_packet_id_t)> oncancel);
  void async_close(std::function<void()>);

  void reply(a0_packet_id_t, const Packet&);
};

class RpcClient {
 public:
  std::shared_ptr<a0_rpc_client_t> c;

  RpcClient(ShmObj);
  void async_close(std::function<void()>);

  void send_cb(const Packet&, std::function<void(Packet)>);
  std::future<Packet> send_fut(const Packet&);
  void cancel(a0_packet_id_t);
};

}  // namespace a0;
