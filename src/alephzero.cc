#include <a0/alephzero.h>

#include <a0/internal/macros.h>
#include <a0/internal/strutil.hh>

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace {

struct subscriber_def {
  std::string container;
  std::string topic;
  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;
};

void to_json(json& j, const subscriber_def& def) {
  j = json{{"container", def.container}, {"topic", def.topic}};

  if (def.read_start == A0_READ_START_EARLIEST) {
    j["read_start"] = "EARLIEST";
  } else if (def.read_start == A0_READ_START_LATEST) {
    j["read_start"] = "LATEST";
  } else if (def.read_start == A0_READ_START_NEW) {
    j["read_start"] = "NEW";
  } else {
    throw;
  }

  if (def.read_next == A0_READ_NEXT_SEQUENTIAL) {
    j["read_next"] = "SEQUENTIAL";
  } else if (def.read_next == A0_READ_NEXT_RECENT) {
    j["read_next"] = "RECENT";
  } else {
    throw;
  }
}

void from_json(const json& j, subscriber_def& def) {
  j.at("container").get_to(def.container);
  j.at("topic").get_to(def.topic);

  std::string read_start_str;
  j.at("read_start").get_to(read_start_str);

  if (read_start_str == "EARLIEST") {
    def.read_start = A0_READ_START_EARLIEST;
  } else if (read_start_str == "LATEST") {
    def.read_start = A0_READ_START_LATEST;
  } else if (read_start_str == "NEW") {
    def.read_start = A0_READ_START_NEW;
  } else {
    throw;
  }

  std::string read_next_str;
  j.at("read_next").get_to(read_next_str);

  if (read_next_str == "SEQUENTIAL") {
    def.read_next = A0_READ_NEXT_SEQUENTIAL;
  } else if (read_next_str == "RECENT") {
    def.read_next = A0_READ_NEXT_RECENT;
  } else {
    throw;
  }
}

struct rpc_client_def {
  std::string container;
  std::string topic;
};

void to_json(json& j, const rpc_client_def& def) {
  j = json{{"container", def.container}, {"topic", def.topic}};
}

void from_json(const json& j, rpc_client_def& def) {
  j.at("container").get_to(def.container);
  j.at("topic").get_to(def.topic);
}

struct alephzero_options {
  std::string container;

  std::unordered_map<std::string, subscriber_def> subscriber_defs;
  std::unordered_map<std::string, rpc_client_def> rpc_client_defs;
};

void to_json(json& j, const alephzero_options& opts) {
  j = json{{"container", opts.container},
           {"subscriber_defs", opts.subscriber_defs},
           {"rpc_client_defs", opts.rpc_client_defs}};
}

void from_json(const json& j, alephzero_options& opts) {
  j.at("container").get_to(opts.container);
  j.at("subscriber_defs").get_to(opts.subscriber_defs);
  j.at("rpc_client_defs").get_to(opts.rpc_client_defs);
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
  for (size_t i = 0; i < copts.num_subscriber_defs; i++) {
    opts->subscriber_defs[copts.subscriber_defs[i].name] = {
        .container = copts.subscriber_defs[i].container,
        .topic = copts.subscriber_defs[i].topic,
        .read_start = copts.subscriber_defs[i].read_start,
        .read_next = copts.subscriber_defs[i].read_next,
    };
  }
  for (size_t i = 0; i < copts.num_rpc_client_defs; i++) {
    opts->rpc_client_defs[copts.rpc_client_defs[i].name] = {
        .container = copts.rpc_client_defs[i].container,
        .topic = copts.rpc_client_defs[i].topic,
    };
  }

  return A0_OK;
}

errno_t a0_alephzero_close(a0_alephzero_t*);

void a0_publisher_managed_finalizer(a0_publisher_t*, std::function<void()>);
void a0_subscriber_sync_managed_finalizer(a0_subscriber_sync_t*, std::function<void()>);
void a0_subscriber_zero_copy_managed_finalizer(a0_subscriber_zero_copy_t*, std::function<void()>);
void a0_subscriber_managed_finalizer(a0_subscriber_t*, std::function<void()>);
void a0_rpc_server_managed_finalizer(a0_rpc_server_t*, std::function<void()>);
void a0_rpc_client_managed_finalizer(a0_rpc_client_t*, std::function<void()>);

errno_t a0_publisher_init(a0_publisher_t* publisher, a0_alephzero_t alephzero, const char* name) {
  auto path = a0::strutil::fmt("/a0_pubsub__%s__%s", alephzero._impl->opts.container.c_str(), name);
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_publisher_init_unmanaged(publisher, shmobj));

  a0_publisher_managed_finalizer(publisher, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* subscriber_sync,
                                a0_alephzero_t alephzero,
                                const char* name) {
  auto* def = &alephzero._impl->opts.subscriber_defs.at(name);
  auto path = a0::strutil::fmt("/a0_pubsub__%s__%s", def->container.c_str(), def->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_subscriber_sync_init_unmanaged(subscriber_sync, shmobj, def->read_start, def->read_next));

  a0_subscriber_sync_managed_finalizer(subscriber_sync, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_zero_copy_init(a0_subscriber_zero_copy_t* subscriber_zero_copy,
                                     a0_alephzero_t alephzero,
                                     const char* name,
                                     a0_zero_copy_callback_t zero_copy_callback) {
  auto* def = &alephzero._impl->opts.subscriber_defs.at(name);
  auto path = a0::strutil::fmt("/a0_pubsub__%s__%s", def->container.c_str(), def->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_subscriber_zero_copy_init_unmanaged(subscriber_zero_copy,
                                                                       shmobj,
                                                                       def->read_start,
                                                                       def->read_next,
                                                                       zero_copy_callback));

  a0_subscriber_zero_copy_managed_finalizer(subscriber_zero_copy, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_subscriber_init(a0_subscriber_t* subscriber,
                           a0_alephzero_t alephzero,
                           const char* name,
                           a0_alloc_t alloc,
                           a0_packet_callback_t packet_callback) {
  auto* def = &alephzero._impl->opts.subscriber_defs.at(name);
  auto path = a0::strutil::fmt("/a0_pubsub__%s__%s", def->container.c_str(), def->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_subscriber_init_unmanaged(subscriber,
                                                             shmobj,
                                                             def->read_start,
                                                             def->read_next,
                                                             alloc,
                                                             packet_callback));

  a0_subscriber_managed_finalizer(subscriber, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_rpc_server_init(a0_rpc_server_t* rpc_server,
                           a0_alephzero_t alephzero,
                           const char* name,
                           a0_alloc_t alloc,
                           a0_packet_callback_t onrequest,
                           a0_packet_callback_t oncancel) {
  auto path = a0::strutil::fmt("/a0_rpc__%s__%s", alephzero._impl->opts.container.c_str(), name);
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(
      a0_rpc_server_init_unmanaged(rpc_server, shmobj, alloc, onrequest, oncancel));

  a0_rpc_server_managed_finalizer(rpc_server, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}

errno_t a0_rpc_client_init(a0_rpc_client_t* rpc_client,
                           a0_alephzero_t alephzero,
                           const char* name,
                           a0_alloc_t alloc) {
  auto* def = &alephzero._impl->opts.rpc_client_defs.at(name);
  auto path = a0::strutil::fmt("/a0_rpc__%s__%s", def->container.c_str(), def->topic.c_str());
  a0_shmobj_t shmobj;
  A0_INTERNAL_RETURN_ERR_ON_ERR(alephzero._impl->incref_shmobj(path, &shmobj));
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_rpc_client_init_unmanaged(rpc_client, shmobj, alloc));

  a0_rpc_client_managed_finalizer(rpc_client, [alephzero, path]() {
    alephzero._impl->decref_shmobj(path);
  });

  return A0_OK;
}
