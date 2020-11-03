#include <a0/alephzero.hpp>
#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/heartbeat.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/topic_manager.h>
#include <a0/uuid.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "alloc_util.hpp"
#include "macros.h"
#include "scope.hpp"
#include "strutil.hpp"

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
  explicit CDeleter(std::function<void(C*)> primary)
      : primary{std::move(primary)} {}

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
    delete c;
  }
};

template <typename C, typename InitFn, typename Closer>
void set_c(std::shared_ptr<C>* c, InitFn&& init, Closer&& closer) {
  set_c(c, std::forward<InitFn>(init), CDeleter<C>(std::forward<Closer>(closer)));
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
void check(std::string_view fn_name, const details::CppWrap<T>* cpp_wrap) {
  if (!cpp_wrap || !cpp_wrap->c) {
    auto msg = strutil::cat("AlephZero method called with NULL object: ", fn_name);
    fprintf(stderr, "%s\n", msg.c_str());
    throw std::runtime_error(msg);
  }

  if (cpp_wrap->magic_number != 0xA0A0A0A0) {
    auto msg = strutil::cat("AlephZero method called with corrupt object: ", fn_name);
    fprintf(stderr, "%s\n", msg.c_str());
    // This error is often the result of a throw in another thread. Let that propagate first.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    throw std::runtime_error(msg);
  }
}

#define CHECK_C \
  check(__PRETTY_FUNCTION__, this)

template <typename T>
a0_buf_t as_buf(T* mem) {
  return a0_buf_t{
      .ptr = (uint8_t*)(mem->data()),
      .size = mem->size(),
  };
}

std::string_view as_string_view(a0_buf_t buf) {
  return std::string_view((char*)buf.ptr, buf.size);
}

// TODO(lshamis): Is this the right response?
#define TRY(NAME, BODY)                                          \
  try {                                                          \
    BODY;                                                        \
  } catch (const std::exception& e) {                            \
    fprintf(stderr, NAME " threw an exception: %s\n", e.what()); \
    std::terminate();                                            \
  } catch (...) {                                                \
    fprintf(stderr, NAME " threw an exception: ???\n");          \
    std::terminate();                                            \
  }

}  // namespace

size_t Arena::size() const {
  CHECK_C;
  return c->size;
}

File::Options File::Options::DEFAULT = {
    .create_options = {
        .size = A0_FILE_OPTIONS_DEFAULT.create_options.size,
        .mode = A0_FILE_OPTIONS_DEFAULT.create_options.mode,
        .dir_mode = A0_FILE_OPTIONS_DEFAULT.create_options.dir_mode,
    },
    .open_options = {
        .readonly = A0_FILE_OPTIONS_DEFAULT.open_options.readonly,
    },
};

File::File(std::string_view path)
    : File(path, Options::DEFAULT) {}

File::File(std::string_view path, Options opts) {
  a0_file_options_t c_opts{
      .create_options = {
          .size = opts.create_options.size,
          .mode = opts.create_options.mode,
          .dir_mode = opts.create_options.dir_mode,
      },
      .open_options = {
          .readonly = opts.open_options.readonly,
      },
  };
  set_c(
      &c,
      [&](a0_file_t* c) {
        return a0_file_open(path.data(), &c_opts, c);
      },
      a0_file_close);
}

File::operator Arena() const {
  CHECK_C;
  auto save = c;
  return make_cpp<Arena>(
      [&](a0_arena_t* arena) {
        *arena = c->arena;
        return A0_OK;
      },
      [save](a0_arena_t*) {});
}

size_t File::size() const {
  CHECK_C;
  return c->arena.size;
}

std::string File::path() const {
  CHECK_C;
  return c->path;
}

int File::fd() const {
  CHECK_C;
  return c->fd;
}

stat_t File::stat() const {
  CHECK_C;
  return c->stat;
}

void File::remove(std::string_view path) {
  auto err = a0_file_remove(path.data());
  // Ignore "No such file or directory" errors.
  if (err == ENOENT) {
    return;
  }

  check(err);
}

void File::remove_all(std::string_view path) {
  check(a0_file_remove_all(path.data()));
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
std::shared_ptr<a0_packet_t> make_cpp_packet(std::string_view id) {
  std::shared_ptr<a0_packet_t> c(new a0_packet_t, PacketImpl{});
  memset(&*c, 0, sizeof(a0_packet_t));

  if (id.empty()) {
    // Create a new ID.
    check(a0_packet_init(&*c));
  } else if (A0_LIKELY(id.size() == A0_UUID_SIZE - 1)) {
    memcpy(c->id, id.data(), A0_UUID_SIZE - 1);
    c->id[A0_UUID_SIZE - 1] = '\0';
  } else {
    // TODO(lshamis): Handle corrupt ids.
    throw;
  }

  return c;
}

A0_STATIC_INLINE
void cpp_packet_add_headers(std::shared_ptr<a0_packet_t>* c,
                            std::vector<std::pair<std::string, std::string>> hdrs) {
  auto* impl = std::get_deleter<PacketImpl>(*c);

  impl->cpp_hdrs = std::move(hdrs);

  for (size_t i = 0; i < impl->cpp_hdrs.size(); i++) {
    impl->c_hdrs.push_back(a0_packet_header_t{
        .key = impl->cpp_hdrs[i].first.c_str(),
        .val = impl->cpp_hdrs[i].second.c_str(),
    });
  }

  (*c)->headers_block = {
      .headers = impl->c_hdrs.data(),
      .size = impl->c_hdrs.size(),
      .next_block = nullptr,
  };
}

A0_STATIC_INLINE
void cpp_packet_add_payload(std::shared_ptr<a0_packet_t>* c,
                            std::string_view payload) {
  (*c)->payload = as_buf(&payload);
}

A0_STATIC_INLINE
void cpp_packet_add_payload(std::shared_ptr<a0_packet_t>* c,
                            std::string payload) {
  auto* impl = std::get_deleter<PacketImpl>(*c);
  impl->cpp_payload = std::move(payload);
  cpp_packet_add_payload(c, std::string_view(impl->cpp_payload));
}

PacketView::PacketView()
    : PacketView(std::string_view{}) {}

PacketView::PacketView(std::string_view payload)
    : PacketView({}, payload) {}

PacketView::PacketView(std::vector<std::pair<std::string, std::string>> headers,
                       std::string_view payload) {
  c = make_cpp_packet();
  cpp_packet_add_headers(&c, std::move(headers));
  cpp_packet_add_payload(&c, payload);
}

PacketView::PacketView(const Packet& pkt) {
  c = make_cpp_packet(pkt.id());
  cpp_packet_add_headers(&c, pkt.headers());
  cpp_packet_add_payload(&c, pkt.payload());
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
  cpp_packet_add_headers(&c, std::move(hdrs));
  cpp_packet_add_payload(&c, as_string_view(pkt.payload));
}

std::string_view PacketView::id() const {
  CHECK_C;
  return c->id;
}

const std::vector<std::pair<std::string, std::string>>& PacketView::headers() const {
  CHECK_C;
  auto* impl = std::get_deleter<PacketImpl>(c);
  return impl->cpp_hdrs;
}

std::string_view PacketView::payload() const {
  CHECK_C;
  return as_string_view(c->payload);
}

Packet::Packet()
    : Packet(std::string{}) {}

Packet::Packet(std::string payload)
    : Packet({}, std::move(payload)) {}

Packet::Packet(std::vector<std::pair<std::string, std::string>> headers,
               std::string payload) {
  c = make_cpp_packet();
  cpp_packet_add_headers(&c, std::move(headers));
  cpp_packet_add_payload(&c, std::move(payload));
}

Packet::Packet(const PacketView& view) {
  c = make_cpp_packet(view.id());
  cpp_packet_add_headers(&c, view.headers());
  cpp_packet_add_payload(&c, std::string(view.payload()));
}

Packet::Packet(PacketView&& view) {
  c = make_cpp_packet(view.id());
  cpp_packet_add_headers(&c, std::move(std::get_deleter<PacketImpl>(view.c)->cpp_hdrs));
  cpp_packet_add_payload(&c, std::string(view.payload()));
}

Packet::Packet(a0_packet_t pkt)
    : Packet(PacketView(pkt)) {}

std::string_view Packet::id() const {
  CHECK_C;
  return c->id;
}

const std::vector<std::pair<std::string, std::string>>& Packet::headers() const {
  CHECK_C;
  auto* impl = std::get_deleter<PacketImpl>(c);
  return impl->cpp_hdrs;
}

std::string_view Packet::payload() const {
  CHECK_C;
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

File TopicManager::config_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_config_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::heartbeat_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_heartbeat_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::log_crit_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_log_crit_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::log_err_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_log_err_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::log_warn_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_log_warn_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::log_info_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_log_info_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::log_dbg_topic() const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_log_dbg_topic(&ctm, file);
      },
      a0_file_close);
}

File TopicManager::publisher_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_publisher_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

File TopicManager::subscriber_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_subscriber_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

File TopicManager::rpc_server_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_rpc_server_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

File TopicManager::rpc_client_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_rpc_client_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

File TopicManager::prpc_server_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_prpc_server_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

File TopicManager::prpc_client_topic(std::string_view name) const {
  a0_topic_manager_t ctm = c(this);

  return make_cpp<File>(
      [&](a0_file_t* file) {
        return a0_topic_manager_open_prpc_client_topic(&ctm, name.data(), file);
      },
      a0_file_close);
}

TopicManager& GlobalTopicManager() {
  static TopicManager tm;
  return tm;
}

void InitGlobalTopicManager(TopicManager tm) {
  GlobalTopicManager() = std::move(tm);
}

PublisherRaw::PublisherRaw(Arena arena) {
  set_c(
      &c,
      [&](a0_publisher_raw_t* c) {
        return a0_publisher_raw_init(c, *arena.c);
      },
      [arena](a0_publisher_raw_t* c) {
        a0_publisher_raw_close(c);
      });
}

PublisherRaw::PublisherRaw(std::string_view topic)
    : PublisherRaw(GlobalTopicManager().publisher_topic(topic)) {}

void PublisherRaw::pub(const PacketView& pkt) {
  CHECK_C;
  check(a0_pub_raw(&*c, *pkt.c));
}

void PublisherRaw::pub(std::vector<std::pair<std::string, std::string>> headers,
                       std::string_view payload) {
  pub(PacketView(std::move(headers), payload));
}

void PublisherRaw::pub(std::string_view payload) {
  pub({}, payload);
}

Publisher::Publisher(Arena arena) {
  set_c(
      &c,
      [&](a0_publisher_t* c) {
        return a0_publisher_init(c, *arena.c);
      },
      [arena](a0_publisher_t* c) {
        a0_publisher_close(c);
      });
}

Publisher::Publisher(std::string_view topic)
    : Publisher(GlobalTopicManager().publisher_topic(topic)) {}

void Publisher::pub(const PacketView& pkt) {
  CHECK_C;
  check(a0_pub(&*c, *pkt.c));
}

void Publisher::pub(std::vector<std::pair<std::string, std::string>> headers,
                    std::string_view payload) {
  pub(PacketView(std::move(headers), payload));
}

void Publisher::pub(std::string_view payload) {
  pub({}, payload);
}

SubscriberSync::SubscriberSync(Arena arena, a0_subscriber_init_t init, a0_subscriber_iter_t iter) {
  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  set_c(
      &c,
      [&](a0_subscriber_sync_t* c) {
        return a0_subscriber_sync_init(c, *arena.c, alloc, init, iter);
      },
      [arena, alloc](a0_subscriber_sync_t* c) mutable {
        a0_subscriber_sync_close(c);
        a0_realloc_allocator_close(&alloc);
      });
}

SubscriberSync::SubscriberSync(std::string_view topic,
                               a0_subscriber_init_t init,
                               a0_subscriber_iter_t iter)
    : SubscriberSync(GlobalTopicManager().subscriber_topic(topic), init, iter) {}

bool SubscriberSync::has_next() {
  CHECK_C;
  bool has_next;
  check(a0_subscriber_sync_has_next(&*c, &has_next));
  return has_next;
}

PacketView SubscriberSync::next() {
  CHECK_C;
  a0_packet_t pkt;
  check(a0_subscriber_sync_next(&*c, &pkt));
  return pkt;
}

Subscriber::Subscriber(Arena arena,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(const PacketView&)> fn) {
  CDeleter<a0_subscriber_t> deleter;
  deleter.also.emplace_back([arena]() {});

  auto* heap_fn = new std::function<void(const PacketView&)>(std::move(fn));
  a0_packet_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            TRY("a0::Subscriber callback",
                (*(std::function<void(const PacketView&)>*)user_data)(pkt));
          },
  };
  deleter.also.emplace_back([heap_fn]() {
    delete heap_fn;
  });

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_subscriber_close;

  set_c(
      &c,
      [&](a0_subscriber_t* c) {
        return a0_subscriber_init(c, *arena.c, alloc, init, iter, callback);
      },
      std::move(deleter));
}

Subscriber::Subscriber(std::string_view topic,
                       a0_subscriber_init_t init,
                       a0_subscriber_iter_t iter,
                       std::function<void(const PacketView&)> fn)
    : Subscriber(GlobalTopicManager().subscriber_topic(topic), init, iter, std::move(fn)) {}

void Subscriber::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::Subscriber::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_subscriber_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_subscriber_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::Subscriber::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_subscriber_async_close(&*heap_data->c, callback));
}

Packet Subscriber::read_one(Arena arena, a0_subscriber_init_t init, int flags) {
  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  a0::scope<void> alloc_destroyer([&]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  a0_packet_t pkt;
  check(a0_subscriber_read_one(*arena.c, alloc, init, flags, &pkt));
  return Packet(pkt);
}

Packet Subscriber::read_one(std::string_view topic, a0_subscriber_init_t init, int flags) {
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
                  std::string_view payload) {
  write_config(tm, PacketView(std::move(headers), payload));
}

void write_config(const TopicManager& tm, std::string_view payload) {
  write_config(tm, {}, payload);
}

RpcServer RpcRequest::server() {
  RpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_rpc_server_t>(c->server, [](a0_rpc_server_t*) {});
  return server;
}

PacketView RpcRequest::pkt() {
  CHECK_C;
  return c->pkt;
}

void RpcRequest::reply(const PacketView& pkt) {
  CHECK_C;
  check(a0_rpc_reply(*c, *pkt.c));
}

void RpcRequest::reply(std::vector<std::pair<std::string, std::string>> headers,
                       std::string_view payload) {
  reply(PacketView(std::move(headers), payload));
}

void RpcRequest::reply(std::string_view payload) {
  reply({}, payload);
}

RpcServer::RpcServer(Arena arena,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(std::string_view)> oncancel) {
  CDeleter<a0_rpc_server_t> deleter;
  deleter.also.emplace_back([arena]() {});

  auto* heap_onrequest = new std::function<void(RpcRequest)>(std::move(onrequest));
  deleter.also.emplace_back([heap_onrequest]() {
    delete heap_onrequest;
  });
  a0_rpc_request_callback_t c_onrequest = {
      .user_data = heap_onrequest,
      .fn =
          [](void* user_data, a0_rpc_request_t c_req) {
            RpcRequest req;
            req.c = std::make_shared<a0_rpc_request_t>(c_req);
            TRY("a0::RpcServer::onrequest callback",
                (*(std::function<void(RpcRequest)>*)user_data)(req));
          },
  };

  a0_packet_id_callback_t c_oncancel = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (oncancel) {
    auto* heap_oncancel = new std::function<void(std::string_view)>(std::move(oncancel));
    deleter.also.emplace_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_uuid_t id) {
              TRY("a0::RpcServer::oncancel callback",
                  (*(std::function<void(std::string_view)>*)user_data)(id));
            },
    };
  }

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_rpc_server_close;

  set_c(
      &c,
      [&](a0_rpc_server_t* c) {
        return a0_rpc_server_init(c, *arena.c, alloc, c_onrequest, c_oncancel);
      },
      std::move(deleter));
}

RpcServer::RpcServer(std::string_view topic,
                     std::function<void(RpcRequest)> onrequest,
                     std::function<void(std::string_view)> oncancel)
    : RpcServer(GlobalTopicManager().rpc_server_topic(topic),
                std::move(onrequest),
                std::move(oncancel)) {}

void RpcServer::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::RpcServer::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_server_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::RpcServer::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_rpc_server_async_close(&*heap_data->c, callback));
}

RpcClient::RpcClient(Arena arena) {
  CDeleter<a0_rpc_client_t> deleter;
  deleter.also.emplace_back([arena]() {});

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_rpc_client_close;

  set_c(
      &c,
      [&](a0_rpc_client_t* c) {
        return a0_rpc_client_init(c, *arena.c, alloc);
      },
      std::move(deleter));
}

RpcClient::RpcClient(std::string_view topic)
    : RpcClient(GlobalTopicManager().rpc_client_topic(topic)) {}

void RpcClient::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::RpcClient::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_rpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_rpc_client_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::RpcClient::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_rpc_client_async_close(&*heap_data->c, callback));
}

void RpcClient::send(const PacketView& pkt, std::function<void(const PacketView&)> fn) {
  CHECK_C;
  a0_packet_callback_t callback = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (fn) {
    auto* heap_fn = new std::function<void(const PacketView&)>(std::move(fn));
    callback = {
        .user_data = heap_fn,
        .fn =
            [](void* user_data, a0_packet_t pkt) {
              auto* fn = (std::function<void(const PacketView&)>*)user_data;
              TRY("a0::RpcClient::send callback", (*fn)(pkt));
              delete fn;
            },
    };
  }
  check(a0_rpc_send(&*c, *pkt.c, callback));
}

void RpcClient::send(std::vector<std::pair<std::string, std::string>> headers,
                     std::string_view payload,
                     std::function<void(const PacketView&)> cb) {
  send(PacketView(std::move(headers), payload), std::move(cb));
}

void RpcClient::send(std::string_view payload, std::function<void(const PacketView&)> cb) {
  send({}, payload, std::move(cb));
}

std::future<Packet> RpcClient::send(const PacketView& pkt) {
  auto p = std::make_shared<std::promise<Packet>>();
  send(pkt, [p](const a0::PacketView& resp) {
    p->set_value(Packet(resp));
  });
  return p->get_future();
}

std::future<Packet> RpcClient::send(std::vector<std::pair<std::string, std::string>> headers,
                                    std::string_view payload) {
  return send(PacketView(std::move(headers), payload));
}

std::future<Packet> RpcClient::send(std::string_view payload) {
  return send({}, payload);
}

void RpcClient::cancel(std::string_view id) {
  CHECK_C;
  check(a0_rpc_cancel(&*c, id.data()));
}

PrpcServer PrpcConnection::server() {
  PrpcServer server;
  // Note: this does not extend the server lifetime.
  server.c = std::shared_ptr<a0_prpc_server_t>(c->server, [](a0_prpc_server_t*) {});
  return server;
}

PacketView PrpcConnection::pkt() {
  CHECK_C;
  return c->pkt;
}

void PrpcConnection::send(const PacketView& pkt, bool done) {
  CHECK_C;
  check(a0_prpc_send(*c, *pkt.c, done));
}

void PrpcConnection::send(std::vector<std::pair<std::string, std::string>> headers,
                          std::string_view payload,
                          bool done) {
  send(PacketView(std::move(headers), payload), done);
}

void PrpcConnection::send(std::string_view payload, bool done) {
  send({}, payload, done);
}

PrpcServer::PrpcServer(Arena arena,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(std::string_view)> oncancel) {
  CDeleter<a0_prpc_server_t> deleter;
  deleter.also.emplace_back([arena]() {});

  auto* heap_onconnect = new std::function<void(PrpcConnection)>(std::move(onconnect));
  deleter.also.emplace_back([heap_onconnect]() {
    delete heap_onconnect;
  });
  a0_prpc_connection_callback_t c_onconnect = {
      .user_data = heap_onconnect,
      .fn =
          [](void* user_data, a0_prpc_connection_t c_req) {
            PrpcConnection req;
            req.c = std::make_shared<a0_prpc_connection_t>(c_req);
            TRY("a0::PrpcServer::onconnect callback",
                (*(std::function<void(PrpcConnection)>*)user_data)(req));
          },
  };

  a0_packet_id_callback_t c_oncancel = {
      .user_data = nullptr,
      .fn = nullptr,
  };
  if (oncancel) {
    auto* heap_oncancel = new std::function<void(std::string_view)>(std::move(oncancel));
    deleter.also.emplace_back([heap_oncancel]() {
      delete heap_oncancel;
    });
    c_oncancel = {
        .user_data = heap_oncancel,
        .fn =
            [](void* user_data, a0_uuid_t id) {
              TRY("a0::PrpcServer::oncancel callback",
                  (*(std::function<void(std::string_view)>*)user_data)(id));
            },
    };
  }

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_prpc_server_close;

  set_c(
      &c,
      [&](a0_prpc_server_t* c) {
        return a0_prpc_server_init(c, *arena.c, alloc, c_onconnect, c_oncancel);
      },
      std::move(deleter));
}

PrpcServer::PrpcServer(std::string_view topic,
                       std::function<void(PrpcConnection)> onconnect,
                       std::function<void(std::string_view)> oncancel)
    : PrpcServer(GlobalTopicManager().prpc_server_topic(topic),
                 std::move(onconnect),
                 std::move(oncancel)) {}

void PrpcServer::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::PrpcServer::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_server_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_server_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::PrpcServer::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_prpc_server_async_close(&*heap_data->c, callback));
}

PrpcClient::PrpcClient(Arena arena) {
  CDeleter<a0_prpc_client_t> deleter;
  deleter.also.emplace_back([arena]() {});

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_prpc_client_close;

  set_c(
      &c,
      [&](a0_prpc_client_t* c) {
        return a0_prpc_client_init(c, *arena.c, alloc);
      },
      std::move(deleter));
}

PrpcClient::PrpcClient(std::string_view topic)
    : PrpcClient(GlobalTopicManager().prpc_client_topic(topic)) {}

void PrpcClient::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::PrpcClient::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_prpc_client_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_prpc_client_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::PrpcClient::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_prpc_client_async_close(&*heap_data->c, callback));
}

void PrpcClient::connect(const PacketView& pkt, std::function<void(const PacketView&, bool)> fn) {
  CHECK_C;
  auto* heap_fn = new std::function<void(const PacketView&, bool)>(std::move(fn));
  a0_prpc_callback_t callback = {
      .user_data = heap_fn,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool done) {
            auto* fn = (std::function<void(const PacketView&, bool)>*)user_data;
            TRY("a0::PrpcClient::connect callback", (*fn)(pkt, done));
            if (done) {
              delete fn;
            }
          },
  };
  check(a0_prpc_connect(&*c, *pkt.c, callback));
}

void PrpcClient::connect(std::vector<std::pair<std::string, std::string>> headers,
                         std::string_view payload,
                         std::function<void(const PacketView&, bool)> cb) {
  connect(PacketView(std::move(headers), payload), std::move(cb));
}
void PrpcClient::connect(std::string_view payload,
                         std::function<void(const PacketView&, bool)> cb) {
  connect({}, payload, std::move(cb));
}

void PrpcClient::cancel(std::string_view id) {
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
                              shm_crit.c->arena,
                              shm_err.c->arena,
                              shm_warn.c->arena,
                              shm_info.c->arena,
                              shm_dbg.c->arena);
      },
      [shm_crit, shm_err, shm_warn, shm_info, shm_dbg](a0_logger_t* c) {
        a0_logger_close(c);
      });
}

Logger::Logger()
    : Logger(GlobalTopicManager()) {}

void Logger::crit(const PacketView& pkt) {
  CHECK_C;
  check(a0_log_crit(&*c, *pkt.c));
}
void Logger::err(const PacketView& pkt) {
  CHECK_C;
  check(a0_log_err(&*c, *pkt.c));
}
void Logger::warn(const PacketView& pkt) {
  CHECK_C;
  check(a0_log_warn(&*c, *pkt.c));
}
void Logger::info(const PacketView& pkt) {
  CHECK_C;
  check(a0_log_info(&*c, *pkt.c));
}
void Logger::dbg(const PacketView& pkt) {
  CHECK_C;
  check(a0_log_dbg(&*c, *pkt.c));
}

Heartbeat::Options Heartbeat::Options::DEFAULT = {
    .freq = A0_HEARTBEAT_OPTIONS_DEFAULT.freq,
};

Heartbeat::Heartbeat(Arena arena, Options opts) {
  a0_heartbeat_options_t c_opts{
      .freq = opts.freq,
  };
  set_c(
      &c,
      [&](a0_heartbeat_t* c) {
        return a0_heartbeat_init(c, *arena.c, &c_opts);
      },
      [arena](a0_heartbeat_t* c) {
        a0_heartbeat_close(c);
      });
}

Heartbeat::Heartbeat(Arena arena)
    : Heartbeat(std::move(arena), Options::DEFAULT) {}

Heartbeat::Heartbeat(Options opts)
    : Heartbeat(GlobalTopicManager().heartbeat_topic(), opts) {}

Heartbeat::Heartbeat()
    : Heartbeat(Options::DEFAULT) {}

HeartbeatListener::Options HeartbeatListener::Options::DEFAULT = {
    .min_freq = A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT.min_freq,
};

HeartbeatListener::HeartbeatListener(Arena arena,
                                     Options opts,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed) {
  a0_heartbeat_listener_options_t c_opts{
      .min_freq = opts.min_freq,
  };

  CDeleter<a0_heartbeat_listener_t> deleter;
  deleter.also.emplace_back([arena]() {});

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
  deleter.also.emplace_back([heap_data]() {
    delete heap_data;
  });

  a0_alloc_t alloc;
  check(a0_realloc_allocator_init(&alloc));
  deleter.also.emplace_back([alloc]() mutable {
    a0_realloc_allocator_close(&alloc);
  });

  deleter.primary = a0_heartbeat_listener_close;

  set_c(
      &c,
      [&](a0_heartbeat_listener_t* c) {
        return a0_heartbeat_listener_init(c, *arena.c, alloc, &c_opts, c_ondetected, c_onmissed);
      },
      std::move(deleter));
}

HeartbeatListener::HeartbeatListener(Arena arena,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(std::move(arena), Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}
HeartbeatListener::HeartbeatListener(std::string_view container,
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
          std::move(ondetected),
          std::move(onmissed)) {}
HeartbeatListener::HeartbeatListener(std::string_view container,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(container, Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}
HeartbeatListener::HeartbeatListener(Options opts,
                                     std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(GlobalTopicManager().container, opts, std::move(ondetected), std::move(onmissed)) {}
HeartbeatListener::HeartbeatListener(std::function<void()> ondetected,
                                     std::function<void()> onmissed)
    : HeartbeatListener(Options::DEFAULT, std::move(ondetected), std::move(onmissed)) {}

void HeartbeatListener::async_close(std::function<void()> fn) {
  if (!c) {
    TRY("a0::HeartbeatListener::async_close callback", fn());
    return;
  }
  auto* deleter = std::get_deleter<CDeleter<a0_heartbeat_listener_t>>(c);
  deleter->primary = nullptr;

  struct data_t {
    std::shared_ptr<a0_heartbeat_listener_t> c;
    std::function<void()> fn;
  };
  auto* heap_data = new data_t{c, std::move(fn)};
  a0_callback_t callback = {
      .user_data = heap_data,
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            TRY("a0::HeartbeatListener::async_close callback", data->fn());
            delete data;
          },
  };

  check(a0_heartbeat_listener_async_close(&*heap_data->c, callback));
}

}  // namespace a0
