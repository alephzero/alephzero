#include <a0/alephzero.hpp>
#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <errno.h>
#include <sched.h>
#include <stddef.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "macros.h"

namespace a0 {
namespace {

void check(errno_t err) {
  if (err) {
    throw std::system_error(err, std::generic_category());
  }
}

template <typename C, typename InitFn, typename CloserFn>
std::shared_ptr<C> c_shared(InitFn&& init, CloserFn&& closer) {
  C* c = new C;
  errno_t err = init(c);
  if (err) {
    delete c;
    check(err);
  }
  return std::shared_ptr<C>(c, std::forward<CloserFn>(closer));
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
    if (c) {
      delete c;
    }
  }
};

A0_STATIC_INLINE
a0_buf_t as_buf(auto* mem) {
  return a0_buf_t{
      .ptr = (uint8_t*)(mem->data()),
      .size = mem->size(),
  };
}

A0_STATIC_INLINE
std::string_view as_string_view(a0_buf_t buf) {
  return std::string_view((char*)buf.ptr, buf.size);
}

}  // namespace

Shm::Shm(const std::string& path) {
  c = c_shared<a0_shm_t>(
      [&](a0_shm_t* c) {
        return a0_shm_open(path.c_str(), nullptr, c);
      },
      delete_after<a0_shm_t>(a0_shm_close));
}

Shm::Shm(const std::string& path, const Options& opts) {
  a0_shm_options_t c_shm_opts{
      .size = opts.size,
  };
  c = c_shared<a0_shm_t>(
      [&](a0_shm_t* c) {
        return a0_shm_open(path.c_str(), &c_shm_opts, c);
      },
      delete_after<a0_shm_t>(a0_shm_close));
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
  return as_string_view(c_payload);
}

std::string PacketView::id() const {
  a0_packet_id_t c_id;
  check(a0_packet_id(c, &c_id));
  return c_id;
}

const a0_packet_t Packet::c() const {
  return as_buf(&mem);
}

Packet::Packet() : Packet("") {}

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
            *out = as_buf(mem);
          },
  };

  std::vector<a0_packet_header_t> c_hdrs;
  for (auto&& hdr : hdrs) {
    c_hdrs.push_back({hdr.first.data(), hdr.second.data()});
  }

  check(a0_packet_build({c_hdrs.data(), c_hdrs.size()}, as_buf(&payload), alloc, nullptr));
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
  c = c_shared<a0_topic_manager_t>(
      [&](a0_topic_manager_t* c) {
        return a0_topic_manager_init(c, json.c_str());
      },
      delete_after<a0_topic_manager_t>(a0_topic_manager_close));
}

std::string_view TopicManager::container_name() const {
  const char* out;
  a0_topic_manager_container_name(&*c, &out);
  return out;
}

Shm TopicManager::config_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_config_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::log_crit_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_crit_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::log_err_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_err_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::log_warn_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_warn_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::log_info_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_info_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::log_dbg_topic() const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_dbg_topic(&*c, shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::publisher_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_publisher_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::subscriber_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_subscriber_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::rpc_server_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_rpc_server_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::rpc_client_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_rpc_client_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::prpc_server_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_prpc_server_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
}

Shm TopicManager::prpc_client_topic(const std::string& name) const {
  return to_cpp<Shm>(c_shared<a0_shm_t>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_prpc_client_topic(&*c, name.c_str(), shm);
      },
      delete_after<a0_shm_t>(a0_shm_close)));
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
  c = c_shared<a0_publisher_t>(
      [&](a0_publisher_t* c) {
        return a0_publisher_init(c, shm.c->buf);
      },
      [shm](a0_publisher_t* c) {
        a0_publisher_close(c);
        delete c;
      });
}

Publisher::Publisher(const std::string& topic)
    : Publisher(global_topic_manager().publisher_topic(topic)) {}

void Publisher::pub(const Packet& pkt) {
  check(a0_pub(&*c, pkt.c()));
}

void Publisher::pub(std::string_view payload) {
  check(a0_pub_emplace(&*c, {}, as_buf(&payload), nullptr));
}

Logger::Logger(const TopicManager& topic_manager) {
  auto shm_crit = topic_manager.log_crit_topic();
  auto shm_err = topic_manager.log_err_topic();
  auto shm_warn = topic_manager.log_warn_topic();
  auto shm_info = topic_manager.log_info_topic();
  auto shm_dbg = topic_manager.log_dbg_topic();
  c = c_shared<a0_logger_t>(
      [&](a0_logger_t* c) {
        return a0_logger_init(c,
                              shm_crit.c->buf,
                              shm_err.c->buf,
                              shm_warn.c->buf,
                              shm_info.c->buf,
                              shm_dbg.c->buf);
      },
      [shm_crit, shm_err, shm_warn, shm_info, shm_dbg](a0_logger_t* c) {
        a0_logger_close(c);
        delete c;
      });
}

Logger::Logger() : Logger(global_topic_manager()) {}

void Logger::crit(const Packet& pkt) {
  check(a0_log_crit(&*c, pkt.c()));
}
void Logger::crit(std::string_view payload) {
  crit(Packet(payload));
}
void Logger::err(const Packet& pkt) {
  check(a0_log_err(&*c, pkt.c()));
}
void Logger::err(std::string_view payload) {
  err(Packet(payload));
}
void Logger::warn(const Packet& pkt) {
  check(a0_log_warn(&*c, pkt.c()));
}
void Logger::warn(std::string_view payload) {
  warn(Packet(payload));
}
void Logger::info(const Packet& pkt) {
  check(a0_log_info(&*c, pkt.c()));
}
void Logger::info(std::string_view payload) {
  info(Packet(payload));
}
void Logger::dbg(const Packet& pkt) {
  check(a0_log_dbg(&*c, pkt.c()));
}
void Logger::dbg(std::string_view payload) {
  dbg(Packet(payload));
}

SubscriberSync::SubscriberSync(Shm shm, a0_subscriber_init_t init, a0_subscriber_iter_t iter) {
  auto alloc = a0_realloc_allocator();
  c = c_shared<a0_subscriber_sync_t>(
      [&](a0_subscriber_sync_t* c) {
        return a0_subscriber_sync_init(c, shm.c->buf, alloc, init, iter);
      },
      [shm, alloc](a0_subscriber_sync_t* c) {
        a0_subscriber_sync_close(c);
        a0_free_realloc_allocator(alloc);
        delete c;
      });
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

  c = c_shared<a0_subscriber_t>(
      [&](a0_subscriber_t* c) {
        return a0_subscriber_init(c, shm.c->buf, alloc, init, iter, callback);
      },
      deleter);
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
  struct scope_t {
    a0_alloc_t alloc;
    scope_t() : alloc{a0_realloc_allocator()} {}
    ~scope_t() {
      a0_free_realloc_allocator(alloc);
    }
  } scope;

  PacketView pkt_view;
  check(a0_subscriber_read_one(shm.c->buf, scope.alloc, init, flags, &pkt_view.c));
  Packet pkt(pkt_view);

  return pkt;
}

Packet Subscriber::read_one(const std::string& topic, a0_subscriber_init_t init, int flags) {
  return Subscriber::read_one(global_topic_manager().subscriber_topic(topic), init, flags);
}

Subscriber onconfig(std::function<void(PacketView)> fn) {
  return Subscriber(global_topic_manager().config_topic(),
                    A0_INIT_MOST_RECENT,
                    A0_ITER_NEWEST,
                    std::move(fn));
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
  check(a0_rpc_reply_emplace(*c, {}, as_buf(&payload), nullptr));
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
  a0_rpc_request_callback_t c_onrequest = {
      .user_data = heap_onrequest,
      .fn =
          [](void* user_data, a0_rpc_request_t c_req) {
            (*(std::function<void(RpcRequest)>*)user_data)(
                RpcRequest{std::make_shared<a0_rpc_request_t>(c_req)});
          },
  };

  a0_packet_id_callback_t c_oncancel = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (oncancel) {
    auto heap_oncancel = new std::function<void(std::string)>(std::move(oncancel));
    deleter.also.push_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_packet_id_t id) {
              (*(std::function<void(std::string)>*)user_data)(id);
            },
    };
  }

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_rpc_server_close;

  c = c_shared<a0_rpc_server_t>(
      [&](a0_rpc_server_t* c) {
        return a0_rpc_server_init(c, shm.c->buf, alloc, c_onrequest, c_oncancel);
      },
      deleter);
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

  c = c_shared<a0_rpc_client_t>(
      [&](a0_rpc_client_t* c) {
        return a0_rpc_client_init(c, shm.c->buf, alloc);
      },
      deleter);
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
  a0_packet_callback_t callback = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (fn) {
    auto heap_fn = new std::function<void(PacketView)>(std::move(fn));
    callback = {
        .user_data = heap_fn,
        .fn =
            [](void* user_data, a0_packet_t c_pkt) {
              auto* fn = (std::function<void(PacketView)>*)user_data;
              (*fn)(PacketView{.c = c_pkt});
              delete fn;
            },
    };
  }
  check(a0_rpc_send(&*c, pkt.c(), callback));
}

void RpcClient::send(std::string_view payload, std::function<void(PacketView)> fn) {
  send(Packet(payload), std::move(fn));
}

std::future<Packet> RpcClient::send(const Packet& pkt) {
  auto p = std::make_shared<std::promise<Packet>>();
  send(pkt, [p](a0::PacketView reply) {
    p->set_value(reply);
  });
  return p->get_future();
}

std::future<Packet> RpcClient::send(std::string_view payload) {
  return send(Packet(payload));
}

void RpcClient::cancel(const std::string& id) {
  check(a0_rpc_cancel(&*c, id.c_str()));
}

PrpcServer PrpcConnection::server() {
  PrpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_prpc_server_t>(c->server, [](a0_prpc_server_t*) {});
  return server;
}

PacketView PrpcConnection::pkt() {
  return PacketView{.c = c->pkt};
}

void PrpcConnection::send(const Packet& pkt, bool done) {
  check(a0_prpc_send(*c, pkt.c(), done));
}

void PrpcConnection::send(std::string_view payload, bool done) {
  send(Packet(payload), done);
}

PrpcServer::PrpcServer(Shm shm,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(std::string)> oncancel) {
  CDeleter<a0_prpc_server_t> deleter;
  deleter.also.push_back([shm]() {});

  auto heap_onconnect = new std::function<void(PrpcConnection)>(std::move(onconnect));
  deleter.also.push_back([heap_onconnect]() {
    delete heap_onconnect;
  });
  a0_prpc_connection_callback_t c_onconnect = {
      .user_data = heap_onconnect,
      .fn =
          [](void* user_data, a0_prpc_connection_t c_req) {
            (*(std::function<void(PrpcConnection)>*)user_data)(
                PrpcConnection{std::make_shared<a0_prpc_connection_t>(c_req)});
          },
  };

  a0_packet_id_callback_t c_oncancel = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (oncancel) {
    auto heap_oncancel = new std::function<void(std::string)>(std::move(oncancel));
    deleter.also.push_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_packet_id_t id) {
              (*(std::function<void(std::string)>*)user_data)(id);
            },
    };
  }

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_prpc_server_close;

  c = c_shared<a0_prpc_server_t>(
      [&](a0_prpc_server_t* c) {
        return a0_prpc_server_init(c, shm.c->buf, alloc, c_onconnect, c_oncancel);
      },
      deleter);
}

PrpcServer::PrpcServer(const std::string& topic,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(std::string)> oncancel)
    : PrpcServer(global_topic_manager().prpc_server_topic(topic),
                 std::move(onconnect),
                 std::move(oncancel)) {}

void PrpcServer::async_close(std::function<void()> fn) {
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_server_t> c;
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

  check(a0_prpc_server_async_close(&*heap_data->c, callback));
}

PrpcClient::PrpcClient(Shm shm) {
  CDeleter<a0_prpc_client_t> deleter;
  deleter.also.push_back([shm]() {});

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_prpc_client_close;

  c = c_shared<a0_prpc_client_t>(
      [&](a0_prpc_client_t* c) {
        return a0_prpc_client_init(c, shm.c->buf, alloc);
      },
      deleter);
}

PrpcClient::PrpcClient(const std::string& topic)
    : PrpcClient(global_topic_manager().prpc_client_topic(topic)) {}

void PrpcClient::async_close(std::function<void()> fn) {
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_client_t> c;
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

  check(a0_prpc_client_async_close(&*heap_data->c, callback));
}

void PrpcClient::connect(const Packet& pkt, std::function<void(PacketView, bool)> fn) {
  auto heap_fn = new std::function<void(PacketView, bool)>(std::move(fn));
  a0_prpc_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t c_pkt, bool done) {
            auto* fn = (std::function<void(PacketView, bool)>*)user_data;
            (*fn)(PacketView{.c = c_pkt}, done);
            if (done) {
              delete fn;
            }
          },
  };
  check(a0_prpc_connect(&*c, pkt.c(), callback));
}

void PrpcClient::connect(std::string_view payload, std::function<void(PacketView, bool)> fn) {
  connect(Packet(payload), std::move(fn));
}

void PrpcClient::cancel(const std::string& id) {
  check(a0_prpc_cancel(&*c, id.c_str()));
}

}  // namespace a0
