#include <alephzero.hpp>
#include <system_error>

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

ShmObj::ShmObj(const std::string& path) {
  c = c_shared<a0_shmobj_t>(a0_shmobj_close);
  check(a0_shmobj_open(path.c_str(), nullptr, &*c));
}
ShmObj::ShmObj(const std::string& path, const Options& opts) {
  a0_shmobj_options_t c_shmobj_opts{
      .size = opts.size,
  };
  c = c_shared<a0_shmobj_t>(a0_shmobj_close);
  check(a0_shmobj_open(path.c_str(), &c_shmobj_opts, &*c));
}
void ShmObj::unlink(const std::string& path) {
  check(a0_shmobj_unlink(path.c_str()));
}

const a0_packet_t Packet::c() const {
  return a0_packet_t{.ptr = (uint8_t*)mem.data(), .size = mem.size()};
}

Packet Packet::build(const std::vector<std::pair<std::string, std::string>>& hdrs,
                     const std::string& payload) {
  Packet pkt;
  a0_alloc_t alloc = {
      .user_data = &pkt,
      .fn =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* mem = &((Packet*)user_data)->mem;
            mem->resize(size);
            *out = {
                .ptr = (uint8_t*)mem->data(),
                .size = mem->size(),
            };
          },
  };

  std::vector<a0_packet_header_t> c_hdrs;
  for (auto&& hdr : hdrs) {
    c_hdrs.push_back({hdr.first.c_str(), hdr.second.c_str()});
  }

  check(a0_packet_build(c_hdrs.size(),
                        c_hdrs.data(),
                        a0_buf_t{
                            .ptr = (uint8_t*)payload.data(),
                            .size = payload.size(),
                        },
                        alloc,
                        nullptr));
  return pkt;
}

size_t Packet::num_headers() const {
  size_t out;
  check(a0_packet_num_headers(c(), &out));
  return out;
}

std::pair<std::string, std::string> Packet::header(size_t idx) const {
  a0_packet_header_t c_hdr;
  check(a0_packet_header(c(), idx, &c_hdr));
  return {c_hdr.key, c_hdr.val};
}

std::string Packet::payload() const {
  a0_buf_t c_payload;
  check(a0_packet_payload(c(), &c_payload));
  return std::string((char*)c_payload.ptr, c_payload.size);
}

std::string Packet::id() const {
  a0_packet_id_t c_id;
  check(a0_packet_id(c(), &c_id));
  return c_id;
}

Publisher::Publisher(ShmObj shmobj) {
  c = c_shared<a0_publisher_t>([shmobj](a0_publisher_t* pub) {
    return a0_publisher_close(pub);
  });
  check(a0_publisher_init(&*c, *shmobj.c));
}

void Publisher::pub(const Packet& pkt) {
  check(a0_pub(&*c, pkt.c()));
}

SubscriberSync::SubscriberSync(ShmObj shmobj,
                               a0_subscriber_init_t init,
                               a0_subscriber_iter_t iter) {
  auto alloc = a0_realloc_allocator();
  c = c_shared<a0_subscriber_sync_t>([shmobj, alloc](a0_subscriber_sync_t* sub_sync) {
    auto err = a0_subscriber_sync_close(sub_sync);
    a0_free_realloc_allocator(alloc);
    return err;
  });
  check(a0_subscriber_sync_init(&*c, *shmobj.c, alloc, init, iter));
}

bool SubscriberSync::has_next() {
  bool has_next;
  check(a0_subscriber_sync_has_next(&*c, &has_next));
  return has_next;
}

Packet SubscriberSync::next() {
  a0_packet_t c_pkt;
  check(a0_subscriber_sync_next(&*c, &c_pkt));
  return Packet{std::string((char*)c_pkt.ptr, c_pkt.size)};
}

Subscriber::Subscriber(ShmObj shmobj,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(Packet)> fn) {
  CDeleter<a0_subscriber_t> deleter;
  deleter.also.push_back([shmobj]() {});

  auto heap_fn = new std::function<void(Packet)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t c_pkt) {
            (*(std::function<void(Packet)>*)user_data)(
                Packet{std::string((char*)c_pkt.ptr, c_pkt.size)});
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
  check(a0_subscriber_init(&*c, *shmobj.c, alloc, init, iter, callback));
}

void Subscriber::async_close(std::function<void()> fn) {
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

  check(a0_subscriber_async_close(&*c, callback));
  c = nullptr;
}

RpcServer RpcRequest::server() {
  RpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_rpc_server_t>(c->server, [](a0_rpc_server_t*) {});
  return server;
}

Packet RpcRequest::pkt() {
  return Packet{std::string((char*)c->pkt.ptr, c->pkt.size)};
}

void RpcRequest::reply(const Packet& pkt) {
  check(a0_rpc_reply(*c, pkt.c()));
}

RpcServer::RpcServer(ShmObj shmobj,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(std::string)> oncancel) {
  CDeleter<a0_rpc_server_t> deleter;
  deleter.also.push_back([shmobj]() {});

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
  check(a0_rpc_server_init(&*c, *shmobj.c, alloc, c_onrequest, c_oncancel));
}

void RpcServer::async_close(std::function<void()> fn) {
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

  check(a0_rpc_server_async_close(&*c, callback));
  c = nullptr;
}

RpcClient::RpcClient(ShmObj shmobj) {
  CDeleter<a0_rpc_client_t> deleter;
  deleter.also.push_back([shmobj]() {});

  auto alloc = a0_realloc_allocator();
  deleter.also.push_back([alloc]() {
    a0_free_realloc_allocator(alloc);
  });

  deleter.primary = a0_rpc_client_close;

  c = c_shared<a0_rpc_client_t>(deleter);
  check(a0_rpc_client_init(&*c, *shmobj.c, alloc));
}

void RpcClient::async_close(std::function<void()> fn) {
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

  check(a0_rpc_client_async_close(&*c, callback));
  c = nullptr;
}

void RpcClient::send(const Packet& pkt, std::function<void(Packet)> fn) {
  auto heap_fn = new std::function<void(Packet)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t c_pkt) {
            auto* fn = (std::function<void(Packet)>*)user_data;
            (*fn)(Packet{std::string((char*)c_pkt.ptr, c_pkt.size)});
            delete fn;
          },
  };
  check(a0_rpc_send(&*c, pkt.c(), callback));
}

void RpcClient::cancel(const std::string& id) {
  check(a0_rpc_cancel(&*c, id.c_str()));
}

TopicManager::TopicManager(const std::string& json) {
  c = c_shared<a0_topic_manager_t>(a0_topic_manager_close);
  check(a0_topic_manager_init_jsonstr(&*c, json.c_str()));
}

ShmObj TopicManager::config_topic() {
  auto shmobj = to_cpp<ShmObj>(c_shared<a0_shmobj_t>(a0_shmobj_close));
  check(a0_topic_manager_open_config_topic(&*c, &*shmobj.c));
  return shmobj;
}

ShmObj TopicManager::publisher_topic(const std::string& name) {
  auto shmobj = to_cpp<ShmObj>(c_shared<a0_shmobj_t>(a0_shmobj_close));
  check(a0_topic_manager_open_publisher_topic(&*c, name.c_str(), &*shmobj.c));
  return shmobj;
}

ShmObj TopicManager::subscriber_topic(const std::string& name) {
  auto shmobj = to_cpp<ShmObj>(c_shared<a0_shmobj_t>(a0_shmobj_close));
  check(a0_topic_manager_open_subscriber_topic(&*c, name.c_str(), &*shmobj.c));
  return shmobj;
}

ShmObj TopicManager::rpc_server_topic(const std::string& name) {
  auto shmobj = to_cpp<ShmObj>(c_shared<a0_shmobj_t>(a0_shmobj_close));
  check(a0_topic_manager_open_rpc_server_topic(&*c, name.c_str(), &*shmobj.c));
  return shmobj;
}

ShmObj TopicManager::rpc_client_topic(const std::string& name) {
  auto shmobj = to_cpp<ShmObj>(c_shared<a0_shmobj_t>(a0_shmobj_close));
  check(a0_topic_manager_open_rpc_client_topic(&*c, name.c_str(), &*shmobj.c));
  return shmobj;
}

}  // namespace a0
