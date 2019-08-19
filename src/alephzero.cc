#include <a0/alephzero.h>

#include <a0/internal/macros.h>
#include <a0/internal/strutil.hh>

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace {

struct topic_map_val {
  std::string container;
  std::string topic;
};

// Unused.
/*
void to_json(json& j, const topic_map_val& val) {
  j = json{{"container", val.container}, {"topic", val.topic}};
}
*/

void from_json(const json& j, topic_map_val& val) {
  j.at("container").get_to(val.container);
  j.at("topic").get_to(val.topic);
}

struct alephzero_options {
  std::string container;

  std::unordered_map<std::string, topic_map_val> subscriber_maps;
  std::unordered_map<std::string, topic_map_val> rpc_client_maps;
};

// Unused.
/*
void to_json(json& j, const alephzero_options& opts) {
  j = json{{"container", opts.container},
           {"subscriber_maps", opts.subscriber_maps},
           {"rpc_client_maps", opts.rpc_client_maps}};
}
*/

void from_json(const json& j, alephzero_options& opts) {
  j.at("container").get_to(opts.container);
  if (j.count("subscriber_maps")) {
    j.at("subscriber_maps").get_to(opts.subscriber_maps);
  }
  if (j.count("rpc_client_maps")) {
    j.at("rpc_client_maps").get_to(opts.rpc_client_maps);
  }
}

a0_alloc_t new_alloc() {
  auto* heap_buf = new a0_buf_t;
  heap_buf->ptr = (uint8_t*)malloc(1);
  heap_buf->size = 1;

  return a0_alloc_t{
      .user_data = heap_buf,
      .fn =
          [](void* user_data, size_t size, a0_buf_t* out) {
            auto* heap_buf = (a0_buf_t*)user_data;
            if (heap_buf->size < size) {
              heap_buf->ptr = (uint8_t*)realloc(heap_buf->ptr, size);
              heap_buf->size = size;
            }
            out->ptr = heap_buf->ptr;
            out->size = size;
          },
  };
}

void delete_alloc(a0_alloc_t alloc) {
  auto* heap_buf = (a0_buf_t*)alloc.user_data;
  free(heap_buf->ptr);
  delete heap_buf;
}

}  // namespace

struct a0_alephzero_impl_s {
  alephzero_options opts;

  // TODO: Read default and path specific shmobj_options from config.
  a0_shmobj_options_t default_shmobj_options;
  std::unordered_map<std::string, std::pair<int, a0_shmobj_t>> open_shmobj;
  std::mutex shmobj_mu;

  a0_alephzero_impl_s() {
    default_shmobj_options.size = 16 * 1024 * 1024;
  }

  errno_t incref_shmobj(std::string path, a0_shmobj_t* out) {
    std::unique_lock<std::mutex> lk{shmobj_mu};

    if (!open_shmobj.count(path)) {
      A0_INTERNAL_RETURN_ERR_ON_ERR(
          a0_shmobj_open(path.c_str(), &default_shmobj_options, &open_shmobj[path].second));
    }

    open_shmobj[path].first++;
    *out = open_shmobj[path].second;
    return A0_OK;
  }

  void decref_shmobj(std::string path) {
    std::unique_lock<std::mutex> lk{shmobj_mu};

    if (open_shmobj.count(path)) {
      open_shmobj[path].first--;
      if (open_shmobj[path].first <= 0) {
        a0_shmobj_close(&open_shmobj[path].second);
        open_shmobj.erase(path);
      }
    }
  }
};

errno_t a0_alephzero_init(a0_alephzero_t* alephzero) {
  using json = nlohmann::json;

  char* cfg_str = getenv("A0_CFG");
  if (!cfg_str) {
    return EINVAL;
  }

  alephzero_options opts;
  try {
    json::parse(cfg_str).get_to(opts);
  } catch (...) {
    return EINVAL;
  }

  alephzero->_impl = new a0_alephzero_impl_t;
  alephzero->_impl->opts = std::move(opts);

  return A0_OK;
}

errno_t a0_alephzero_init_explicit(a0_alephzero_t* alephzero, a0_alephzero_options_t copts) {
  alephzero->_impl = new a0_alephzero_impl_t;
  auto* opts = &alephzero->_impl->opts;

  opts->container = copts.container;
  for (size_t i = 0; i < copts.num_subscriber_maps; i++) {
    opts->subscriber_maps[copts.subscriber_maps[i].name] = {
        .container = copts.subscriber_maps[i].container,
        .topic = copts.subscriber_maps[i].topic,
    };
  }
  for (size_t i = 0; i < copts.num_rpc_client_maps; i++) {
    opts->rpc_client_maps[copts.rpc_client_maps[i].name] = {
        .container = copts.rpc_client_maps[i].container,
        .topic = copts.rpc_client_maps[i].topic,
    };
  }

  return A0_OK;
}

errno_t a0_alephzero_close(a0_alephzero_t* alephzero) {
  if (!alephzero || !alephzero->_impl) {
    return ESHUTDOWN;
  }

  {
    std::unique_lock<std::mutex> lk{alephzero->_impl->shmobj_mu};
    if (!alephzero->_impl->open_shmobj.empty()) {
      return EBUSY;
    }
  }

  delete alephzero->_impl;
  alephzero->_impl = nullptr;

  return A0_OK;
}

void a0_publisher_managed_finalizer(a0_publisher_t*, std::function<void()>);
void a0_subscriber_sync_zc_managed_finalizer(a0_subscriber_sync_zc_t*, std::function<void()>);
void a0_subscriber_sync_managed_finalizer(a0_subscriber_sync_t*, std::function<void()>);
void a0_subscriber_zc_managed_finalizer(a0_subscriber_zc_t*, std::function<void()>);
void a0_subscriber_managed_finalizer(a0_subscriber_t*, std::function<void()>);
void a0_rpc_server_managed_finalizer(a0_rpc_server_t*, std::function<void()>);
void a0_rpc_client_managed_finalizer(a0_rpc_client_t*, std::function<void()>);

const char kConfigTopicTemplate[] = "/a0_config__%s";
const char kPubsubTopicTemplate[] = "/a0_pubsub__%s__%s";
const char kRpcTopicTemplate[] = "/a0_rpc__%s__%s";

errno_t a0_config_sync_init(a0_subscriber_sync_t* sub_sync, a0_alephzero_t alephzero) {
  auto path = a0::strutil::fmt(kConfigTopicTemplate, alephzero._impl->opts.container.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_subscriber_sync_init_unmanaged(sub_sync,
                                                                  shmobj,
                                                                  alloc,
                                                                  A0_READ_START_LATEST,
                                                                  A0_READ_NEXT_RECENT));

  a0_subscriber_sync_managed_finalizer(sub_sync, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}

errno_t a0_config_init(a0_subscriber_t* sub,
                       a0_alephzero_t alephzero,
                       a0_packet_callback_t packet_callback) {
  auto path = a0::strutil::fmt(kConfigTopicTemplate, alephzero._impl->opts.container.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_subscriber_init_unmanaged(sub,
                                                             shmobj,
                                                             alloc,
                                                             A0_READ_START_LATEST,
                                                             A0_READ_NEXT_RECENT,
                                                             packet_callback));

  a0_subscriber_managed_finalizer(sub, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}

errno_t a0_publisher_init(a0_publisher_t* pub, a0_alephzero_t alephzero, const char* name) {
  auto path = a0::strutil::fmt(kPubsubTopicTemplate, alephzero._impl->opts.container.c_str(), name);
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_publisher_init_unmanaged(pub, shmobj));

  a0_publisher_managed_finalizer(pub, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_alephzero_t alephzero,
                                   const char* name,
                                   a0_subscriber_read_start_t read_start,
                                   a0_subscriber_read_next_t read_next) {
  if (!alephzero._impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &alephzero._impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_subscriber_sync_zc_init_unmanaged(sub_sync_zc, shmobj, read_start, read_next));

  a0_subscriber_sync_zc_managed_finalizer(sub_sync_zc, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                a0_alephzero_t alephzero,
                                const char* name,
                                a0_subscriber_read_start_t read_start,
                                a0_subscriber_read_next_t read_next) {
  if (!alephzero._impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &alephzero._impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_subscriber_sync_init_unmanaged(sub_sync, shmobj, alloc, read_start, read_next));

  a0_subscriber_sync_managed_finalizer(sub_sync, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
                              a0_alephzero_t alephzero,
                              const char* name,
                              a0_subscriber_read_start_t read_start,
                              a0_subscriber_read_next_t read_next,
                              a0_zero_copy_callback_t zc_callback) {
  if (!alephzero._impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &alephzero._impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_subscriber_zc_init_unmanaged(sub_zc, shmobj, read_start, read_next, zc_callback));

  a0_subscriber_zc_managed_finalizer(sub_zc, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_alephzero_t alephzero,
                           const char* name,
                           a0_subscriber_read_start_t read_start,
                           a0_subscriber_read_next_t read_next,
                           a0_packet_callback_t packet_callback) {
  if (!alephzero._impl->opts.subscriber_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &alephzero._impl->opts.subscriber_maps.at(name);
  auto path =
      a0::strutil::fmt(kPubsubTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_subscriber_init_unmanaged(sub, shmobj, alloc, read_start, read_next, packet_callback));

  a0_subscriber_managed_finalizer(sub, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}

errno_t a0_rpc_server_init(a0_rpc_server_t* rpc_server,
                           a0_alephzero_t alephzero,
                           const char* name,
                           a0_packet_callback_t onrequest,
                           a0_packet_callback_t oncancel) {
  auto path = a0::strutil::fmt(kRpcTopicTemplate, alephzero._impl->opts.container.c_str(), name);
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_rpc_server_init_unmanaged(rpc_server, shmobj, alloc, onrequest, oncancel));

  a0_rpc_server_managed_finalizer(rpc_server, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}

errno_t a0_rpc_client_init(a0_rpc_client_t* rpc_client,
                           a0_alephzero_t alephzero,
                           const char* name) {
  if (!alephzero._impl->opts.rpc_client_maps.count(name)) {
    return EINVAL;
  }
  auto* mapping = &alephzero._impl->opts.rpc_client_maps.at(name);
  auto path =
      a0::strutil::fmt(kRpcTopicTemplate, mapping->container.c_str(), mapping->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  auto alloc = new_alloc();
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_rpc_client_init_unmanaged(rpc_client, shmobj, alloc));

  a0_rpc_client_managed_finalizer(rpc_client, [alephzero, path, alloc]() {
    alephzero._impl->decref_shmobj(path);
    delete_alloc(alloc);
  });

  return A0_OK;
}
