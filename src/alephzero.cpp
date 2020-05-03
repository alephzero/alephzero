#include <a0/alephzero.hpp>
#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/heartbeat.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/shm.h>
#include <a0/topic_manager.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "alloc_util.hpp"
#include "macros.h"
#include "scope.hpp"

namespace a0 {
namespace {

void check(errno_t err) {
  if (err) {
    throw std::system_error(err, std::generic_category());
  }
}

template <typename C>
struct CDeleter {
  std::function<void(C*)> primary;
  std::vector<std::function<void()>> also;

  CDeleter() = default;
  CDeleter(std::function<void(C*)> primary) : primary{std::move(primary)} {}

  CDeleter(const CDeleter&) = delete;
  CDeleter(CDeleter&&) noexcept = default;
  CDeleter& operator=(const CDeleter&) = delete;
  CDeleter& operator=(CDeleter&&) noexcept = default;

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

template <typename C, typename InitFn, typename Closer>
void set_c(std::shared_ptr<C>* c, InitFn&& init, Closer&& closer) {
  set_c(c, std::forward<InitFn>(init), CDeleter<C>(std::move(closer)));
}

template <typename C, typename InitFn>
void set_c(std::shared_ptr<C>* c, InitFn&& init, CDeleter<C> deleter) {
  *c = std::shared_ptr<C>(new C, std::move(deleter));
  errno_t err = init(c->get());
  if (err) {
    std::get_deleter<CDeleter<C>>(*c)->primary = nullptr;
    *c = nullptr;
    check(err);
  }
}

template <typename CPP, typename InitFn, typename Closer>
CPP make_cpp(InitFn&& init, Closer&& closer) {
  CPP cpp;
  set_c(&cpp.c, std::forward<InitFn>(init), std::forward<Closer>(closer));
  return cpp;
}

template <typename T>
A0_STATIC_INLINE a0_buf_t as_buf(T* mem) {
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

Shm::Options Shm::Options::DEFAULT = {
    .size = A0_SHM_OPTIONS_DEFAULT.size,
    .resize = A0_SHM_OPTIONS_DEFAULT.resize,
};

Shm::Shm(const std::string_view path) : Shm(path, Options::DEFAULT) {}

Shm::Shm(const std::string_view path, Options opts) {
  a0_shm_options_t c_opts{
      .size = opts.size,
      .resize = opts.resize,
  };
  set_c(
      &c,
      [&](a0_shm_t* c) {
        return a0_shm_open(path.data(), &c_opts, c);
      },
      a0_shm_close);
}

std::string Shm::path() const {
  return c->path;
}

void Shm::unlink(const std::string_view path) {
  auto err = a0_shm_unlink(path.data());
  // Ignore "No such file or directory" errors.
  if (err == ENOENT) {
    return;
  }

  check(err);
}

struct PacketImpl {
  std::string cpp_payload;
  std::vector<std::pair<std::string, std::string>> cpp_hdrs;
  std::vector<a0_packet_header_t> c_hdrs;

  void operator()(a0_packet_t* self) {
    delete self;
  }
};

A0_STATIC_INLINE
std::shared_ptr<a0_packet_t> make_cpp_packet() {
  auto* c = new a0_packet_t;
  errno_t err = a0_packet_init(c);
  if (err) {
    delete c;
    check(err);
  }
  return std::shared_ptr<a0_packet_t>(c, PacketImpl{});
}

A0_STATIC_INLINE
std::shared_ptr<a0_packet_t> make_cpp_packet(const std::string_view id) {
  std::shared_ptr<a0_packet_t> c(new a0_packet_t, PacketImpl{});
  memset(&*c, 0, sizeof(a0_packet_t));

  if (id.empty()) {
    // Create a new ID.
    check(a0_packet_init(&*c));
  } else if (A0_LIKELY(id.size() == A0_UUID_SIZE - 1)) {
    memcpy(c->id, id.data(), A0_UUID_SIZE - 1);
    c->id[A0_UUID_SIZE - 1] = '\0';
  } else {
    throw;  // TODO
  }

  return c;
}

A0_STATIC_INLINE
void cpp_packet_add_headers(std::shared_ptr<a0_packet_t>& c,
                            std::vector<std::pair<std::string, std::string>> hdrs) {
  auto* impl = std::get_deleter<PacketImpl>(c);

  impl->cpp_hdrs = std::move(hdrs);

  for (size_t i = 0; i < impl->cpp_hdrs.size(); i++) {
    impl->c_hdrs.push_back(a0_packet_header_t{
        .key = impl->cpp_hdrs[i].first.c_str(),
        .val = impl->cpp_hdrs[i].second.c_str(),
    });
  }

  c->headers_block = {
      .headers = impl->c_hdrs.data(),
      .size = impl->c_hdrs.size(),
      .next_block = nullptr,
  };
}

A0_STATIC_INLINE
void cpp_packet_add_payload(std::shared_ptr<a0_packet_t>& c, const std::string_view payload) {
  c->payload = as_buf(&payload);
}

A0_STATIC_INLINE
void cpp_packet_add_payload(std::shared_ptr<a0_packet_t>& c, std::string payload) {
  auto* impl = std::get_deleter<PacketImpl>(c);
  impl->cpp_payload = std::move(payload);
  cpp_packet_add_payload(c, std::string_view(impl->cpp_payload));
}

PacketView::PacketView() : PacketView(std::string_view{}) {}

PacketView::PacketView(const std::string_view payload) : PacketView({}, payload) {}

PacketView::PacketView(std::vector<std::pair<std::string, std::string>> hdrs,
                       const std::string_view payload) {
  c = make_cpp_packet();
  cpp_packet_add_headers(c, std::move(hdrs));
  cpp_packet_add_payload(c, payload);
}

PacketView::PacketView(const Packet& pkt) {
  c = make_cpp_packet(pkt.id());
  cpp_packet_add_headers(c, pkt.headers());
  cpp_packet_add_payload(c, pkt.payload());
}

PacketView::PacketView(a0_packet_t pkt) {
  std::vector<std::pair<std::string, std::string>> hdrs;
  check(a0_packet_for_each_header(
      pkt.headers_block,
      {.user_data = &hdrs, .fn = [](void* data, a0_packet_header_t hdr) {
         auto* hdrs_ = (std::vector<std::pair<std::string, std::string>>*)data;
         hdrs_->push_back({hdr.key, hdr.val});
       }}));

  c = make_cpp_packet(pkt.id);
  cpp_packet_add_headers(c, std::move(hdrs));
  cpp_packet_add_payload(c, as_string_view(pkt.payload));
}

const std::string_view PacketView::id() const {
  return c->id;
}

const std::vector<std::pair<std::string, std::string>>& PacketView::headers() const {
  auto* impl = std::get_deleter<PacketImpl>(c);
  return impl->cpp_hdrs;
}

const std::string_view PacketView::payload() const {
  return as_string_view(c->payload);
}

Packet::Packet() : Packet(std::string{}) {}

Packet::Packet(std::string payload) : Packet({}, payload) {}

Packet::Packet(std::vector<std::pair<std::string, std::string>> hdrs, std::string payload) {
  c = make_cpp_packet();
  cpp_packet_add_headers(c, std::move(hdrs));
  cpp_packet_add_payload(c, std::move(payload));
}

Packet::Packet(const PacketView& view) {
  c = make_cpp_packet(view.id());
  cpp_packet_add_headers(c, view.headers());
  cpp_packet_add_payload(c, std::string(view.payload()));
}

Packet::Packet(PacketView&& view) {
  c = make_cpp_packet(view.id());
  cpp_packet_add_headers(c, std::move(std::get_deleter<PacketImpl>(view.c)->cpp_hdrs));
  cpp_packet_add_payload(c, std::string(view.payload()));
}

Packet::Packet(a0_packet_t pkt) : Packet(PacketView(pkt)) {}

const std::string_view Packet::id() const {
  return c->id;
}

const std::vector<std::pair<std::string, std::string>>& Packet::headers() const {
  auto* impl = std::get_deleter<PacketImpl>(c);
  return impl->cpp_hdrs;
}

const std::string_view Packet::payload() const {
  return as_string_view(c->payload);
}

A0_STATIC_INLINE
a0_topic_manager_t c(const TopicManager* cpp_) {
  auto copy_aliases = [](const std::map<std::string, TopicAliasTarget>* cpp_aliases,
                         a0_alloc_t alloc) {
    a0_buf_t mem;
    check(a0_alloc(alloc, cpp_aliases->size() * sizeof(a0_topic_alias_t), &mem));
    auto* c_aliases = (a0_topic_alias_t*)mem.ptr;

    size_t i = 0;
    for (auto&& [name, target] : *cpp_aliases) {
      c_aliases[i].name = name.c_str();
      c_aliases[i].target_container = target.container.c_str();
      c_aliases[i].target_topic = target.topic.c_str();
      i++;
    }

    return c_aliases;
  };

  a0_topic_manager_t c_;
  c_.container = cpp_->container.c_str();

  c_.subscriber_aliases_size = cpp_->subscriber_aliases.size();
  c_.rpc_client_aliases_size = cpp_->rpc_client_aliases.size();
  c_.prpc_client_aliases_size = cpp_->prpc_client_aliases.size();

  thread_local a0::scope<a0_alloc_t> subscriber_aliases_alloc = a0::scope_realloc();
  thread_local a0::scope<a0_alloc_t> rpc_client_aliases_alloc = a0::scope_realloc();
  thread_local a0::scope<a0_alloc_t> prpc_client_aliases_alloc = a0::scope_realloc();

  c_.subscriber_aliases = copy_aliases(&cpp_->subscriber_aliases, *subscriber_aliases_alloc);
  c_.rpc_client_aliases = copy_aliases(&cpp_->rpc_client_aliases, *rpc_client_aliases_alloc);
  c_.prpc_client_aliases = copy_aliases(&cpp_->prpc_client_aliases, *prpc_client_aliases_alloc);

  return c_;
}

Shm TopicManager::config_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_config_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::heartbeat_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_heartbeat_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::log_crit_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_crit_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::log_err_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_err_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::log_warn_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_warn_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::log_info_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_info_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::log_dbg_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_log_dbg_topic(&ctm, shm);
      },
      a0_shm_close);
}

Shm TopicManager::publisher_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_publisher_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

Shm TopicManager::subscriber_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_subscriber_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

Shm TopicManager::rpc_server_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_rpc_server_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

Shm TopicManager::rpc_client_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_rpc_client_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

Shm TopicManager::prpc_server_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_prpc_server_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

Shm TopicManager::prpc_client_topic(const std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<Shm>(
      [&](a0_shm_t* shm) {
        return a0_topic_manager_open_prpc_client_topic(&ctm, name.data(), shm);
      },
      a0_shm_close);
}

TopicManager& GlobalTopicManager() {
  static TopicManager tm;
  return tm;
}

void InitGlobalTopicManager(TopicManager tm) {
  GlobalTopicManager() = std::move(tm);
}

PublisherRaw::PublisherRaw(Shm shm) {
  set_c(
      &c,
      [&](a0_publisher_raw_t* c) {
        return a0_publisher_raw_init(c, shm.c->buf);
      },
      [shm](a0_publisher_raw_t* c) {
        a0_publisher_raw_close(c);
      });
}

PublisherRaw::PublisherRaw(const std::string_view topic)
    : PublisherRaw(GlobalTopicManager().publisher_topic(topic)) {}

void PublisherRaw::pub(const PacketView& pkt) {
  check(a0_pub_raw(&*c, *pkt.c));
}

void PublisherRaw::pub(std::vector<std::pair<std::string, std::string>> headers,
                       const std::string_view payload) {
  pub(PacketView(std::move(headers), payload));
}

void PublisherRaw::pub(const std::string_view payload) {
  pub({}, payload);
}

Publisher::Publisher(Shm shm) {
  set_c(
      &c,
      [&](a0_publisher_t* c) {
        return a0_publisher_init(c, shm.c->buf);
      },
      [shm](a0_publisher_t* c) {
        a0_publisher_close(c);
      });
}

Publisher::Publisher(const std::string_view topic)
    : Publisher(GlobalTopicManager().publisher_topic(topic)) {}

void Publisher::pub(const PacketView& pkt) {
  check(a0_pub(&*c, *pkt.c));
}

void Publisher::pub(std::vector<std::pair<std::string, std::string>> headers,
                    const std::string_view payload) {
  pub(PacketView(std::move(headers), payload));
}

void Publisher::pub(const std::string_view payload) {
  pub({}, payload);
}

SubscriberSync::SubscriberSync(Shm shm, a0_subscriber_init_t init, a0_subscriber_iter_t iter) {
  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  set_c(
      &c,
      [&](a0_subscriber_sync_t* c) {
        return a0_subscriber_sync_init(c, shm.c->buf, alloc, init, iter);
      },
      [shm, alloc](a0_subscriber_sync_t* c) mutable {
        a0_subscriber_sync_close(c);
        a0_realloc_allocator_close(&alloc);
      });
}

SubscriberSync::SubscriberSync(const std::string_view topic,
                               a0_subscriber_init_t init,
                               a0_subscriber_iter_t iter)
    : SubscriberSync(GlobalTopicManager().subscriber_topic(topic), init, iter) {}

bool SubscriberSync::has_next() {
  bool has_next;
  check(a0_subscriber_sync_has_next(&*c, &has_next));
  return has_next;
}

PacketView SubscriberSync::next() {
  a0_packet_t pkt;
  check(a0_subscriber_sync_next(&*c, &pkt));
  return pkt;
}

Subscriber::Subscriber(Shm shm,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(const PacketView&)> fn) {
  CDeleter<a0_subscriber_t> deleter;
  deleter.also.push_back([shm]() {});

  auto heap_fn = new std::function<void(const PacketView&)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            (*(std::function<void(const PacketView&)>*)user_data)(pkt);
          },
  };
  deleter.also.push_back([heap_fn]() {
    delete heap_fn;
  });

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_subscriber_close;

  set_c(
      &c,
      [&](a0_subscriber_t* c) {
        return a0_subscriber_init(c, shm.c->buf, alloc, init, iter, callback);
      },
      std::move(deleter));
}

Subscriber::Subscriber(const std::string_view topic,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(const PacketView&)> fn)
    : Subscriber(GlobalTopicManager().subscriber_topic(topic), init, iter, std::move(fn)) {}

void Subscriber::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_subscriber_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_subscriber_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
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
  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  a0::scope<void> alloc_destroyer([&]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  a0_packet_t pkt;
  check(a0_subscriber_read_one(shm.c->buf, alloc, init, flags, &pkt));
  return Packet(pkt);
}

Packet Subscriber::read_one(const std::string_view topic, a0_subscriber_init_t init, int flags) {
  return Subscriber::read_one(GlobalTopicManager().subscriber_topic(topic), init, flags);
}

Subscriber onconfig(std::function<void(const PacketView&)> fn) {
  return Subscriber(GlobalTopicManager().config_topic(),
                    A0_INIT_MOST_RECENT,
                    A0_ITER_NEWEST,
                    std::move(fn));
}
Packet read_config(int flags) {
  return Subscriber::read_one(GlobalTopicManager().config_topic(), A0_INIT_MOST_RECENT, flags);
}
void write_config(const TopicManager& tm, const PacketView& pkt) {
  Publisher p(tm.config_topic());
  p.pub(pkt);
}
void write_config(const TopicManager& tm,
                  std::vector<std::pair<std::string, std::string>> headers,
                  const std::string_view payload) {
  write_config(tm, PacketView(std::move(headers), payload));
}
void write_config(const TopicManager& tm, const std::string_view payload) {
  write_config(tm, {}, payload);
}

RpcServer RpcRequest::server() {
  RpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_rpc_server_t>(c->server, [](a0_rpc_server_t*) {});
  return server;
}

PacketView RpcRequest::pkt() {
  return c->pkt;
}

void RpcRequest::reply(const PacketView& pkt) {
  check(a0_rpc_reply(*c, *pkt.c));
}

void RpcRequest::reply(std::vector<std::pair<std::string, std::string>> headers,
                       const std::string_view payload) {
  reply(PacketView(std::move(headers), payload));
}

void RpcRequest::reply(const std::string_view payload) {
  reply({}, payload);
}

RpcServer::RpcServer(Shm shm,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(const std::string_view)> oncancel) {
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
    auto heap_oncancel = new std::function<void(const std::string_view)>(std::move(oncancel));
    deleter.also.push_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_uuid_t id) {
              (*(std::function<void(const std::string_view)>*)user_data)(id);
            },
    };
  }

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_rpc_server_close;

  set_c(
      &c,
      [&](a0_rpc_server_t* c) {
        return a0_rpc_server_init(c, shm.c->buf, alloc, c_onrequest, c_oncancel);
      },
      std::move(deleter));
}

RpcServer::RpcServer(const std::string_view topic,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(const std::string_view)> oncancel)
    : RpcServer(GlobalTopicManager().rpc_server_topic(topic),
                std::move(onrequest),
                std::move(oncancel)) {}

void RpcServer::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_server_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
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

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_rpc_client_close;

  set_c(
      &c,
      [&](a0_rpc_client_t* c) {
        return a0_rpc_client_init(c, shm.c->buf, alloc);
      },
      std::move(deleter));
}

RpcClient::RpcClient(const std::string_view topic)
    : RpcClient(GlobalTopicManager().rpc_client_topic(topic)) {}

void RpcClient::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_client_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
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

void RpcClient::send(const PacketView& pkt, std::function<void(const PacketView&)> fn) {
  a0_packet_callback_t callback = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (fn) {
    auto heap_fn = new std::function<void(const PacketView&)>(std::move(fn));
    callback = {
        .user_data = heap_fn,
        .fn =
            [](void* user_data, a0_packet_t pkt) {
              auto* fn = (std::function<void(const PacketView&)>*)user_data;
              (*fn)(pkt);
              delete fn;
            },
    };
  }
  check(a0_rpc_send(&*c, *pkt.c, callback));
}

void RpcClient::send(std::vector<std::pair<std::string, std::string>> headers,
                     const std::string_view payload,
                     std::function<void(const PacketView&)> cb) {
  send(PacketView(headers, payload), cb);
}

void RpcClient::send(const std::string_view payload, std::function<void(const PacketView&)> cb) {
  send({}, payload, cb);
}

std::future<Packet> RpcClient::send(const PacketView& pkt) {
  auto p = std::make_shared<std::promise<Packet>>();
  send(pkt, [p](a0::PacketView resp) {
    p->set_value(Packet(resp));
  });
  return p->get_future();
}

std::future<Packet> RpcClient::send(std::vector<std::pair<std::string, std::string>> headers,
                                    const std::string_view payload) {
  return send(PacketView(headers, payload));
}

std::future<Packet> RpcClient::send(const std::string_view payload) {
  return send({}, payload);
}

void RpcClient::cancel(const std::string_view id) {
  check(a0_rpc_cancel(&*c, id.data()));
}

PrpcServer PrpcConnection::server() {
  PrpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_prpc_server_t>(c->server, [](a0_prpc_server_t*) {});
  return server;
}

PacketView PrpcConnection::pkt() {
  return c->pkt;
}

void PrpcConnection::send(const PacketView& pkt, bool done) {
  check(a0_prpc_send(*c, *pkt.c, done));
}

void PrpcConnection::send(std::vector<std::pair<std::string, std::string>> headers,
                          const std::string_view payload,
                          bool done) {
  send(PacketView(std::move(headers), payload), done);
}

void PrpcConnection::send(const std::string_view payload, bool done) {
  send({}, payload, done);
}

PrpcServer::PrpcServer(Shm shm,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(const std::string_view)> oncancel) {
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
    auto heap_oncancel = new std::function<void(const std::string_view)>(std::move(oncancel));
    deleter.also.push_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_uuid_t id) {
              (*(std::function<void(const std::string_view)>*)user_data)(id);
            },
    };
  }

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_prpc_server_close;

  set_c(
      &c,
      [&](a0_prpc_server_t* c) {
        return a0_prpc_server_init(c, shm.c->buf, alloc, c_onconnect, c_oncancel);
      },
      std::move(deleter));
}

PrpcServer::PrpcServer(const std::string_view topic,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(const std::string_view)> oncancel)
    : PrpcServer(GlobalTopicManager().prpc_server_topic(topic),
                 std::move(onconnect),
                 std::move(oncancel)) {}

void PrpcServer::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_server_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
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

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_prpc_client_close;

  set_c(
      &c,
      [&](a0_prpc_client_t* c) {
        return a0_prpc_client_init(c, shm.c->buf, alloc);
      },
      std::move(deleter));
}

PrpcClient::PrpcClient(const std::string_view topic)
    : PrpcClient(GlobalTopicManager().prpc_client_topic(topic)) {}

void PrpcClient::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_client_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
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

void PrpcClient::connect(const PacketView& pkt, std::function<void(const PacketView&, bool)> fn) {
  auto heap_fn = new std::function<void(const PacketView&, bool)>(std::move(fn));
  a0_prpc_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool done) {
            auto* fn = (std::function<void(const PacketView&, bool)>*)user_data;
            (*fn)(pkt, done);
            if (done) {
              delete fn;
            }
          },
  };
  check(a0_prpc_connect(&*c, *pkt.c, callback));
}

void PrpcClient::connect(std::vector<std::pair<std::string, std::string>> headers,
                         const std::string_view payload,
                         std::function<void(const PacketView&, bool)> cb) {
  connect(PacketView(std::move(headers), payload), cb);
}
void PrpcClient::connect(const std::string_view payload,
                         std::function<void(const PacketView&, bool)> cb) {
  connect({}, payload, cb);
}

void PrpcClient::cancel(const std::string_view id) {
  check(a0_prpc_cancel(&*c, id.data()));
}

Logger::Logger(const TopicManager& topic_manager) {
  auto shm_crit = topic_manager.log_crit_topic();
  auto shm_err = topic_manager.log_err_topic();
  auto shm_warn = topic_manager.log_warn_topic();
  auto shm_info = topic_manager.log_info_topic();
  auto shm_dbg = topic_manager.log_dbg_topic();
  set_c(
      &c,
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
      });
}

Logger::Logger() : Logger(GlobalTopicManager()) {}

void Logger::crit(const PacketView& pkt) {
  check(a0_log_crit(&*c, *pkt.c));
}
void Logger::err(const PacketView& pkt) {
  check(a0_log_err(&*c, *pkt.c));
}
void Logger::warn(const PacketView& pkt) {
  check(a0_log_warn(&*c, *pkt.c));
}
void Logger::info(const PacketView& pkt) {
  check(a0_log_info(&*c, *pkt.c));
}
void Logger::dbg(const PacketView& pkt) {
  check(a0_log_dbg(&*c, *pkt.c));
}

Heartbeat::Options Heartbeat::Options::DEFAULT = {
    .freq = A0_HEARTBEAT_OPTIONS_DEFAULT.freq,
};

Heartbeat::Heartbeat(Shm shm, Options opts) {
  a0_heartbeat_options_t c_opts{
      .freq = opts.freq,
  };
  set_c(
      &c,
      [&](a0_heartbeat_t* c) {
        return a0_heartbeat_init(c, shm.c->buf, &c_opts);
      },
      [shm](a0_heartbeat_t* c) {
        a0_heartbeat_close(c);
      });
}

Heartbeat::Heartbeat(Shm shm) : Heartbeat(shm, Options::DEFAULT) {}

Heartbeat::Heartbeat(Options opts) : Heartbeat(GlobalTopicManager().heartbeat_topic(), opts) {}

Heartbeat::Heartbeat() : Heartbeat(Options::DEFAULT) {}

HeartbeatListener::Options HeartbeatListener::Options::DEFAULT = {
    .min_freq = A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT.min_freq,
};

HeartbeatListener::HeartbeatListener(Shm shm,
                                     Options opts,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed) {
  a0_heartbeat_listener_options_t c_opts{
      .min_freq = opts.min_freq,
  };

  CDeleter<a0_heartbeat_listener_t> deleter;
  deleter.also.push_back([shm]() {});

  struct data_t {
    std::function<void()> ondetected;
    std::function<void()> onmissed;
  };
  auto* heap_data = new data_t{std::move(ondetected), std::move(onmissed)};

  a0_callback_t c_ondetected = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            if ((*(data_t*)user_data).ondetected) {
              (*(data_t*)user_data).ondetected();
            }
          },
  };
  a0_callback_t c_onmissed = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            if ((*(data_t*)user_data).onmissed) {
              (*(data_t*)user_data).onmissed();
            }
          },
  };
  deleter.also.push_back([heap_data]() {
    delete heap_data;
  });

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.push_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_heartbeat_listener_close;

  set_c(
      &c,
      [&](a0_heartbeat_listener_t* c) {
        return a0_heartbeat_listener_init(c, shm.c->buf, alloc, &c_opts, c_ondetected, c_onmissed);
      },
      std::move(deleter));
}

HeartbeatListener::HeartbeatListener(Shm shm,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(shm, Options::DEFAULT, ondetected, onmissed) {}
HeartbeatListener::HeartbeatListener(const std::string_view container,
                                     Options opts,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(
          TopicManager{
              .container = std::string(container),
              .subscriber_aliases = {},
              .rpc_client_aliases = {},
              .prpc_client_aliases = {},
          }
              .heartbeat_topic(),
          opts,
          ondetected,
          onmissed) {}
HeartbeatListener::HeartbeatListener(const std::string_view container,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(container, Options::DEFAULT, ondetected, onmissed) {}
HeartbeatListener::HeartbeatListener(Options opts,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(GlobalTopicManager().container, opts, ondetected, onmissed) {}
HeartbeatListener::HeartbeatListener(std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(Options::DEFAULT, ondetected, onmissed) {}

void HeartbeatListener::async_close(std::function<void()> fn) {
  if (!c) {
    fn();
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_heartbeat_listener_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_heartbeat_listener_t> c;
    std::function<void()> fn;
  };
  auto heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            data->fn();
            delete data;
          },
  };

  check(a0_heartbeat_listener_async_close(&*heap_data->c, callback));
}

}  // namespace a0
