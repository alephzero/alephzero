#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/rpc.h>
#include <a0/rpc.hpp>
#include <a0/string_view.hpp>
#include <a0/time.hpp>
#include <a0/uuid.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "c_opts.hpp"
#include "c_wrap.hpp"

namespace a0 {

namespace {

struct RpcServerRequestImpl {
  std::vector<uint8_t> data;
};

}  // namespace

RpcServer RpcRequest::server() {
  // Note: this does not extend the server lifetime.
  return cpp_wrap<RpcServer>(*c->server);
}

Packet RpcRequest::pkt() {
  CHECK_C;
  auto save = c;
  return Packet(c->pkt, [save](a0_packet_t*) {});
}

void RpcRequest::reply(Packet pkt) {
  CHECK_C;
  check(a0_rpc_server_reply(*c, *pkt.c));
}

namespace {

struct RpcServerImpl {
  std::vector<uint8_t> data;
  RpcServer::Options opts;
};

}  // namespace

RpcServer::RpcServer(
    RpcTopic topic,
    std::function<void(RpcRequest)> onrequest)
    : RpcServer(topic, onrequest, nullptr) {}

RpcServer::RpcServer(
    RpcTopic topic,
    std::function<void(RpcRequest)> onrequest,
    std::function<void(string_view /* id */)> oncancel)
    : RpcServer(topic, RpcServer::Options{onrequest, oncancel, TIMEOUT_NEVER}) {}

RpcServer::RpcServer(RpcTopic topic, RpcServer::Options opts) {
  set_c_impl<RpcServerImpl>(
      &c,
      [&](a0_rpc_server_t* c, RpcServerImpl* impl) {
        impl->opts = std::move(opts);

        auto cfo = c_fileopts(topic.file_opts);
        a0_rpc_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (RpcServerImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_rpc_request_callback_t c_onrequest = {
            .user_data = impl,
            .fn = [](void* user_data, a0_rpc_request_t req) {
              auto* impl = (RpcServerImpl*)user_data;

              RpcRequest cpp_req = make_cpp_impl<RpcRequest, RpcServerRequestImpl>(
                  [&](a0_rpc_request_t* c_req, RpcServerRequestImpl* req_impl) {
                    std::swap(impl->data, req_impl->data);
                    *c_req = req;
                    return A0_OK;
                  });

              impl->opts.onrequest(cpp_req);
            }};

        a0_packet_id_callback_t c_oncancel = {
            .user_data = impl,
            .fn = [](void* user_data, a0_uuid_t id) {
              auto* impl = (RpcServerImpl*)user_data;
              impl->opts.oncancel(id);
            }};

        a0_rpc_server_options_t c_opts = {
          .onrequest = c_onrequest,
          .oncancel = c_oncancel,
          .exclusive_ownership_timeout = impl->opts.exclusive_ownership_timeout.c.get(),
        };

        return a0_rpc_server_init(c, c_topic, alloc, c_opts);
      },
      [](a0_rpc_server_t* c, RpcServerImpl*) {
        a0_rpc_server_close(c);
      });
}

namespace {

struct RpcClientTimeoutImpl;

struct RpcClientImpl {
  std::vector<uint8_t> data;

  std::unordered_map<std::string, std::function<void(Packet)>> user_onreply;
  std::unordered_map<uint64_t, std::unique_ptr<RpcClientTimeoutImpl>> user_ontimeout;
  uint64_t next_user_ontimeout_key;
  std::mutex mtx;
};

struct RpcClientTimeoutImpl {
  RpcClientImpl* impl;
  uint64_t key;
  std::function<void()> fn;
};

}  // namespace

RpcClient::RpcClient(RpcTopic topic) {
  set_c_impl<RpcClientImpl>(
      &c,
      [&](a0_rpc_client_t* c, RpcClientImpl*) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_rpc_topic_t c_topic{topic.name.c_str(), &cfo};
        return a0_rpc_client_init(c, c_topic);
      },
      [](a0_rpc_client_t* c, RpcClientImpl*) {
        a0_rpc_client_close(c);
      });
}

void RpcClient::send(Packet pkt, TimeMono timeout, std::function<void(Packet)> onreply, std::function<void()> ontimeout) {
  CHECK_C;
  auto* impl = c_impl<RpcClientImpl>(&c);

  a0_packet_callback_t c_onreply = A0_EMPTY;
  if (onreply) {
    {
      std::unique_lock<std::mutex> lk{impl->mtx};
      impl->user_onreply[std::string(pkt.id())] = std::move(onreply);
    }

    c_onreply = {
        .user_data = impl,
        .fn = [](void* user_data, a0_packet_t resp) {
          auto* impl = (RpcClientImpl*)user_data;

          a0_packet_header_t req_id_hdr;

          a0_packet_header_iterator_t hdr_iter;
          a0_packet_header_iterator_init(&hdr_iter, &resp);
          if (a0_packet_header_iterator_next_match(&hdr_iter, "a0_req_id", &req_id_hdr)) {
            return;
          }

          std::function<void(Packet)> onreply;
          {
            std::unique_lock<std::mutex> lk{impl->mtx};
            auto iter = impl->user_onreply.find(req_id_hdr.val);
            onreply = std::move(iter->second);
            impl->user_onreply.erase(iter);
          }

          onreply(Packet(resp, nullptr));
        },
    };
  }

  a0_callback_t c_ontimeout = A0_EMPTY;
  if (ontimeout) {
    RpcClientTimeoutImpl* timeout_impl;
    {
      std::unique_lock<std::mutex> lk{impl->mtx};
      uint64_t key = impl->next_user_ontimeout_key++;

      timeout_impl = impl->user_ontimeout.insert({
        key,
        std::unique_ptr<RpcClientTimeoutImpl>(new RpcClientTimeoutImpl{impl, key, ontimeout})
      }).first->second.get();
    }

    c_ontimeout = {
        .user_data = timeout_impl,
        .fn = [](void* user_data) {
          auto* timeout_impl = (RpcClientTimeoutImpl*)user_data;
          auto* impl = timeout_impl->impl;
          auto key = timeout_impl->key;
          auto fn = std::move(timeout_impl->fn);
          {
            std::unique_lock<std::mutex> lk{impl->mtx};
            timeout_impl->impl->user_ontimeout.erase(key);
          }
          fn();
          return A0_OK;
        },
    };
  }

  check(a0_rpc_client_send_timeout(&*c, *pkt.c, &*timeout.c, c_onreply, c_ontimeout));
}

std::future<Packet> RpcClient::send(Packet pkt, TimeMono timeout) {
  auto promise = std::make_shared<std::promise<Packet>>();
  auto onreply = [promise](Packet reply) {
    promise->set_value(reply);
  };
  auto ontimeout = [promise]() {
    try {
      check(A0_ERR_TIMEDOUT);
    } catch(...) {
      promise->set_exception(std::current_exception());
    }
  };
  send(pkt, timeout, onreply, ontimeout);
  return promise->get_future();
}

void RpcClient::cancel(string_view id) {
  CHECK_C;
  check(a0_rpc_client_cancel(&*c, id.data()));
}

}  // namespace a0
