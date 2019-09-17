#include <a0/alephzero.hpp>    // for Shm, Packet, TopicManager, PacketView
#include <a0/alloc.h>          // for a0_free_realloc_allocator, a0_realloc_...
#include <a0/common.h>         // for a0_buf_t, a0_callback_t, errno_t
#include <a0/packet.h>         // for a0_packet_header_t, a0_packet_t, a0_pa...
#include <a0/pubsub.h>         // for a0_subscriber_t, a0_publisher_t, a0_su...
#include <a0/rpc.h>            // for a0_rpc_request_t, a0_rpc_server_t, a0_...
#include <a0/shm.h>            // for a0_shm_t, a0_shm_close, a0_shm_open
#include <a0/topic_manager.h>  // for a0_topic_manager_t, a0_topic_manager_init

#include <errno.h>   // for ENOENT
#include <sched.h>   // for memcpy
#include <stddef.h>  // for size_t

#include <cstdint>       // for uint8_t
#include <functional>    // for function
#include <memory>        // for shared_ptr, __shared_ptr_access, make_...
#include <string>        // for string
#include <string_view>   // for string_view, basic_string_view
#include <system_error>  // for generic_category, system_error
#include <utility>       // for move, pair
#include <vector>        // for vector

namespace a0 {
namespace {

void check(errno_t err) {
  if (err) {
    throw std::system_error(err, std::generic_category());
  }
}

template <typename C>
std::shared_ptr<C> c_shared(std::function<void(C*)> closer) {
  return std::shared_ptr<C>(new C, closer);
}

template <typename C>
std::function<void(C*)> delete_after(std::function<void(C*)> closer) {
  return [closer](C* c) {
    closer(c);
    delete c;
  };
}

template <typename CPP, typename C>
CPP to_cpp(std::shared_ptr<C> c) {
  CPP cpp;
  cpp.c = std::move(c);
  return cpp;
}

template <typename C>
struct CDeleter {
  std::function<void(C*)> primary;
  std::vector<std::function<void()>> also;

  void operator()(C* c) {
    if (primary) {
      primary(c);
    }
    for (auto&& fn : also) {
      fn();
    }
  }
};

}  // namespace

Shm::Shm(const std::string& path) {
  c = c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close));
  check(a0_shm_open(path.c_str(), nullptr, &*c));
}

Shm::Shm(const std::string& path, const Options& opts) {
  c = c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close));
  a0_shm_options_t c_shm_opts{
      .size = opts.size,
  };
  check(a0_shm_open(path.c_str(), &c_shm_opts, &*c));
}

std::string Shm::path() const {
  return c->path;
}

void Shm::unlink(const std::string& path) {
  auto err = a0_shm_unlink(path.c_str());
  // Ignore "No such file or directory" errors.
  if (err == ENOENT) {
    return;
  }

  check(err);
}

size_t PacketView::num_headers() const {
  size_t out;
  check(a0_packet_num_headers(c, &out));
  return out;
}

std::pair<std::string_view, std::string_view> PacketView::header(size_t idx) const {
  a0_packet_header_t c_hdr;
  check(a0_packet_header(c, idx, &c_hdr));
  return {c_hdr.key, c_hdr.val};
}

std::string_view PacketView::payload() const {
  a0_buf_t c_payload;
  check(a0_packet_payload(c, &c_payload));
  return std::string_view((char*)c_payload.ptr, c_payload.size);
}

std::string PacketView::id() const {
  a0_packet_id_t c_id;
  check(a0_packet_id(c, &c_id));
  return c_id;
}

const a0_packet_t Packet::c() const {
  return a0_packet_t{.ptr = (uint8_t*)mem.data(), .size = mem.size()};
}

Packet::Packet(PacketView packet_view) {
  mem.resize(packet_view.c.size);
  memcpy(mem.data(), packet_view.c.ptr, packet_view.c.size);
}

Packet::Packet(std::string_view payload) : Packet({}, payload) {}

Packet::Packet(const std::vector<std::pair<std::string_view, std::string_view>>& hdrs,
               std::string_view payload) {
  a0_alloc_t alloc = {
      .user_data = &mem,
      .fn =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* mem = (std::vector<uint8_t>*)user_data;
            mem->resize(size);
            *out = {
                .ptr = (uint8_t*)mem->data(),
                .size = mem->size(),
            };
          },
  };

  std::vector<a0_packet_header_t> c_hdrs;
  for (auto&& hdr : hdrs) {
    c_hdrs.push_back({hdr.first.data(), hdr.second.data()});
  }

  check(a0_packet_build(c_hdrs.size(),
                        c_hdrs.data(),
                        a0_buf_t{
                            .ptr = (uint8_t*)payload.data(),
                            .size = payload.size(),
                        },
                        alloc,
                        nullptr));
}

size_t Packet::num_headers() const {
  size_t out;
  check(a0_packet_num_headers(c(), &out));
  return out;
}

std::pair<std::string_view, std::string_view> Packet::header(size_t idx) const {
  a0_packet_header_t c_hdr;
  check(a0_packet_header(c(), idx, &c_hdr));
  return {c_hdr.key, c_hdr.val};
}

std::string_view Packet::payload() const {
  a0_buf_t c_payload;
  check(a0_packet_payload(c(), &c_payload));
  return std::string_view((char*)c_payload.ptr, c_payload.size);
}

std::string Packet::id() const {
  a0_packet_id_t c_id;
  check(a0_packet_id(c(), &c_id));
  return c_id;
}

TopicManager::TopicManager(const std::string& json) {
  c = c_shared<a0_topic_manager_t>(delete_after<a0_topic_manager_t>(a0_topic_manager_close));
  check(a0_topic_manager_init(&*c, json.c_str()));
}

Shm TopicManager::config_topic() {
  auto shm = to_cpp<Shm>(c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close)));
  check(a0_topic_manager_open_config_topic(&*c, &*shm.c));
  return shm;
}

Shm TopicManager::publisher_topic(const std::string& name) {
  auto shm = to_cpp<Shm>(c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close)));
  check(a0_topic_manager_open_publisher_topic(&*c, name.c_str(), &*shm.c));
  return shm;
}

Shm TopicManager::subscriber_topic(const std::string& name) {
  auto shm = to_cpp<Shm>(c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close)));
  check(a0_topic_manager_open_subscriber_topic(&*c, name.c_str(), &*shm.c));
  return shm;
}

Shm TopicManager::rpc_server_topic(const std::string& name) {
  auto shm = to_cpp<Shm>(c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close)));
  check(a0_topic_manager_open_rpc_server_topic(&*c, name.c_str(), &*shm.c));
  return shm;
}

Shm TopicManager::rpc_client_topic(const std::string& name) {
  auto shm = to_cpp<Shm>(c_shared<a0_shm_t>(delete_after<a0_shm_t>(a0_shm_close)));
  check(a0_topic_manager_open_rpc_client_topic(&*c, name.c_str(), &*shm.c));
  return shm;
}

TopicManager& global_topic_manager() {
  static TopicManager tm;
  return tm;
}

void InitGlobalTopicManager(TopicManager tm) {
  global_topic_manager() = std::move(tm);
}

void InitGlobalTopicManager(const std::string& json) {
  InitGlobalTopicManager(TopicManager(json));
}

Publisher::Publisher(Shm shm) {
  c = c_shared<a0_publisher_t>([shm](a0_publisher_t* p) {
    a0_publisher_close(p);
    delete p;
  });
  check(a0_publisher_init(&*c, shm.c->buf));
}

Publisher::Publisher(const std::string& topic)
    : Publisher(global_topic_manager().publisher_topic(topic)) {}

void Publisher::pub(const Packet& pkt) {
  check(a0_pub(&*c, pkt.c()));
}

void Publisher::pub(std::string_view payload) {
  pub(Packet(payload));
}

SubscriberSync::SubscriberSync(Shm shm, a0_subscriber_init_t init, a0_subscriber_iter_t iter) {
  auto alloc = a0_realloc_allocator();
  c = c_shared<a0_subscriber_sync_t>([shm, alloc](a0_subscriber_sync_t* sub_sync) {
    a0_subscriber_sync_close(sub_sync);
    a0_free_realloc_allocator(alloc);
    delete sub_sync;
  });
  check(a0_subscriber_sync_init(&*c, shm.c->buf, alloc, init, iter));
}

SubscriberSync::SubscriberSync(const std::string& topic,
                               a0_subscriber_init_t init,
                               a0_subscriber_iter_t iter)
    : SubscriberSync(global_topic_manager().subscriber_topic(topic), init, iter) {}

bool SubscriberSync::has_next() {
  bool has_next;
  check(a0_subscriber_sync_has_next(&*c, &has_next));
  return has_next;
}

PacketView SubscriberSync::next() {
  a0_packet_t c_pkt;
  check(a0_subscriber_sync_next(&*c, &c_pkt));
  return PacketView{.c = c_pkt};
}

Subscriber::Subscriber(Shm shm,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(PacketView)> fn) {
  CDeleter<a0_subscriber_t> deleter;
  deleter.also.push_back([shm]() {});

  auto heap_fn = new std::function<void(PacketView)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t c_pkt) {
            (*(std::function<void(PacketView)>*)user_data)(PacketView{.c = c_pkt});
          },
  };
  deleter.also.push_back([heap_fn]() {
    delete heap_fn;
  });

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_subscriber_close;

  c = c_shared<a0_subscriber_t>(deleter);
  check(a0_subscriber_init(&*c, shm.c->buf, alloc, init, iter, callback));
}

Subscriber::Subscriber(const std::string& topic,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(PacketView)> fn)
    : Subscriber(global_topic_manager().subscriber_topic(topic), init, iter, std::move(fn)) {}

void Subscriber::async_close(std::function<void()> fn) {
  auto* deleter = std::get_deleter<CDeleter<a0_subscriber_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_subscriber_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{std::move(c), std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            data->fn();
            delete data;
          },
  };

  check(a0_subscriber_async_close(&*heap_data->c, callback));
}

Packet Subscriber::read_one(Shm shm, a0_subscriber_init_t init, int flags) {
  auto alloc = a0_realloc_allocator();

  PacketView pkt_view;
  a0_subscriber_read_one(shm.c->buf, alloc, init, flags, &pkt_view.c);
  Packet pkt(pkt_view);

  a0_free_realloc_allocator(alloc);

  return pkt;
}

Packet Subscriber::read_one(const std::string& topic, a0_subscriber_init_t init, int flags) {
  return Subscriber::read_one(global_topic_manager().subscriber_topic(topic), init, flags);
}

Packet read_config(int flags) {
  return Subscriber::read_one(global_topic_manager().config_topic(), A0_INIT_MOST_RECENT, flags);
}

RpcServer RpcRequest::server() {
  RpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_rpc_server_t>(c->server, [](a0_rpc_server_t*) {});
  return server;
}

PacketView RpcRequest::pkt() {
  return PacketView{.c = c->pkt};
}

void RpcRequest::reply(const Packet& pkt) {
  check(a0_rpc_reply(*c, pkt.c()));
}

void RpcRequest::reply(std::string_view payload) {
  reply(Packet(payload));
}

RpcServer::RpcServer(Shm shm,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(std::string)> oncancel) {
  CDeleter<a0_rpc_server_t> deleter;
  deleter.also.push_back([shm]() {});

  auto heap_onrequest = new std::function<void(RpcRequest)>(std::move(onrequest));
  deleter.also.push_back([heap_onrequest]() {
    delete heap_onrequest;
  });

  auto heap_oncancel = new std::function<void(std::string)>(std::move(oncancel));
  deleter.also.push_back([heap_oncancel]() {
    delete heap_oncancel;
  });

  a0_rpc_request_callback_t c_onrequest = {
      .user_data = heap_onrequest,
      .fn =
          [](void* user_data, a0_rpc_request_t c_req) {
            (*(std::function<void(RpcRequest)>*)user_data)(
                RpcRequest{std::make_shared<a0_rpc_request_t>(c_req)});
          },
  };
  a0_packet_id_callback_t c_oncancel = {
      .user_data = heap_oncancel,
      .fn =
          [](void* user_data, a0_packet_id_t id) {
            (*(std::function<void(std::string)>*)user_data)(id);
          },
  };

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_rpc_server_close;

  c = c_shared<a0_rpc_server_t>(deleter);
  check(a0_rpc_server_init(&*c, shm.c->buf, alloc, c_onrequest, c_oncancel));
}

RpcServer::RpcServer(const std::string& topic,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(std::string)> oncancel)
    : RpcServer(global_topic_manager().rpc_server_topic(topic),
                std::move(onrequest),
                std::move(oncancel)) {}

void RpcServer::async_close(std::function<void()> fn) {
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_server_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{std::move(c), std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            data->fn();
            delete data;
          },
  };

  check(a0_rpc_server_async_close(&*heap_data->c, callback));
}

RpcClient::RpcClient(Shm shm) {
  CDeleter<a0_rpc_client_t> deleter;
  deleter.also.push_back([shm]() {});

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_rpc_client_close;

  c = c_shared<a0_rpc_client_t>(deleter);
  check(a0_rpc_client_init(&*c, shm.c->buf, alloc));
}

RpcClient::RpcClient(const std::string& topic)
    : RpcClient(global_topic_manager().rpc_client_topic(topic)) {}

void RpcClient::async_close(std::function<void()> fn) {
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_client_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{std::move(c), std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            data->fn();
            delete data;
          },
  };

  check(a0_rpc_client_async_close(&*heap_data->c, callback));
}

void RpcClient::send(const Packet& pkt, std::function<void(PacketView)> fn) {
  auto heap_fn = new std::function<void(PacketView)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t c_pkt) {
            auto* fn = (std::function<void(PacketView)>*)user_data;
            (*fn)(PacketView{.c = c_pkt});
            delete fn;
          },
  };
  check(a0_rpc_send(&*c, pkt.c(), callback));
}

void RpcClient::send(std::string_view payload, std::function<void(PacketView)> fn) {
  send(Packet(payload), std::move(fn));
}

void RpcClient::cancel(const std::string& id) {
  check(a0_rpc_cancel(&*c, id.c_str()));
}

}  // namespace a0
