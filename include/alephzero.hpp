#include <alephzero.h>

#include <functional>
#include <memory>
#include <string>
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

struct TopicManager {
  std::shared_ptr<a0_topic_manager_t> c;

  // TODO: TopicManager(const Options&);
  TopicManager(const std::string& json);

  ShmObj config_topic();
  ShmObj publisher_topic(const std::string&);
  ShmObj subscriber_topic(const std::string&);
  ShmObj rpc_server_topic(const std::string&);
  ShmObj rpc_client_topic(const std::string&);
};

struct Packet {
  std::string mem;
  const a0_packet_t c() const;

  static Packet build(const std::vector<std::pair<std::string, std::string>>& hdrs,
                      const std::string& payload);

  size_t num_headers() const;
  std::pair<std::string, std::string> header(size_t idx) const;
  std::string payload() const;

  std::unique_ptr<std::string> find_header(const std::string& key) const;
  std::string id() const;
};

struct Publisher {
  std::shared_ptr<a0_publisher_t> c;

  Publisher(ShmObj);

  void pub(const Packet&);
};

struct SubscriberSync {
  std::shared_ptr<a0_subscriber_sync_t> c;

  SubscriberSync(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t);

  bool has_next();
  Packet next();
};

struct Subscriber {
  std::shared_ptr<a0_subscriber_t> c;

  Subscriber(ShmObj, a0_subscriber_init_t, a0_subscriber_iter_t, std::function<void(Packet)>);
  void async_close(std::function<void()>);
};

struct RpcServer {
  std::shared_ptr<a0_rpc_server_t> c;

  RpcServer(ShmObj, std::function<void(Packet)> onrequest, std::function<void(std::string)> oncancel);
  void async_close(std::function<void()>);

  void reply(const std::string&, const Packet&);
};

struct RpcClient {
  std::shared_ptr<a0_rpc_client_t> c;

  RpcClient(ShmObj);
  void async_close(std::function<void()>);

  void send(const Packet&, std::function<void(Packet)>);
  void cancel(const std::string&);
};

}  // namespace a0;
