#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/rpc.h>
#include <a0/rpc.hpp>
#include <a0/string_view.hpp>
#include <a0/time.hpp>
#include <a0/unused.h>
#include <a0/uuid.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

RpcServer::RpcServer(RpcTopic topic, Options opts) {
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

struct RpcClientSendImpl;

struct RpcClientImpl {
  std::vector<uint8_t> data;

  std::unordered_map<std::string, std::unique_ptr<RpcClientSendImpl>> send_impl_memory;
  std::mutex mtx;
};

struct RpcClientSendImpl {
  RpcClientImpl* impl;
  std::string pkt_id;
  RpcClient::SendOptions opts;
  std::function<void(Packet)> onreply;
};

}  // namespace

RpcClient::SendOptions::SendOptions() :
    timeout{cpp_wrap<TimeMono>(
        A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.has_timeout ?
            (a0_time_mono_t*)&A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.timeout :
            nullptr)},
    ontimeout{[](SendOptions*) {
      return (Action)A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.ontimeout.fn(nullptr, nullptr);
    }},
    ondisconnect{[](SendOptions*) {
      return (Action)A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.ondisconnect.fn(nullptr, nullptr);
    }},
    onreconnect{[](SendOptions*) {
      return (Action)A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.onreconnect.fn(nullptr, nullptr);
    }},
    oncomplete{[]() {
      return (Action)a0_callback_call(A0_RPC_CLIENT_SEND_OPTIONS_DEFAULT.oncomplete);
    }} {};

RpcClient::RpcClient(RpcTopic topic) {
  set_c_impl<RpcClientImpl>(
      &c,
      [&](a0_rpc_client_t* c, RpcClientImpl* impl) {
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

        return a0_rpc_client_init(c, c_topic, alloc);
      },
      [](a0_rpc_client_t* c, RpcClientImpl*) {
        a0_rpc_client_close(c);
      });
}

A0_STATIC_INLINE
a0_rpc_client_send_options_t install_copts(RpcClientSendImpl* send_impl) {
  a0_rpc_client_send_options_t c_opts;
  a0_rpc_client_send_options_set_timeout(&c_opts, send_impl->opts.timeout.c.get());

  c_opts.ontimeout = {
      .user_data = send_impl,
      .fn = [](void* user_data, a0_rpc_client_send_options_t*) {
        auto* send_impl = (RpcClientSendImpl*)user_data;
        auto ontimeout = send_impl->opts.ontimeout ? send_impl->opts.ontimeout : RpcClient::SendOptions{}.ontimeout;
        auto action = ontimeout(&send_impl->opts);
        install_copts(send_impl);
        return (a0_rpc_client_action_t)action;
      }
  };

  c_opts.ondisconnect = {
      .user_data = send_impl,
      .fn = [](void* user_data, a0_rpc_client_send_options_t*) {
        auto* send_impl = (RpcClientSendImpl*)user_data;
        auto ondisconnect = send_impl->opts.ondisconnect ? send_impl->opts.ondisconnect : RpcClient::SendOptions{}.ondisconnect;
        auto action = send_impl->opts.ondisconnect(&send_impl->opts);
        install_copts(send_impl);
        return (a0_rpc_client_action_t)action;
      }
  };

  c_opts.onreconnect = {
      .user_data = send_impl,
      .fn = [](void* user_data, a0_rpc_client_send_options_t*) {
        auto* send_impl = (RpcClientSendImpl*)user_data;
        auto onreconnect = send_impl->opts.onreconnect ? send_impl->opts.onreconnect : RpcClient::SendOptions{}.onreconnect;
        auto action = send_impl->opts.onreconnect(&send_impl->opts);
        install_copts(send_impl);
        return (a0_rpc_client_action_t)action;
      }
  };

  c_opts.oncomplete = {
      .user_data = send_impl,
      .fn = [](void* user_data) {
        auto* send_impl = (RpcClientSendImpl*)user_data;
        auto oncomplete = send_impl->opts.oncomplete ? send_impl->opts.oncomplete : RpcClient::SendOptions{}.oncomplete;
        // TODO: try-catch?
        if (oncomplete) {
          oncomplete();
        }
        return A0_OK;
      }
  };

  return c_opts;
}

void RpcClient::send(Packet pkt, std::function<void(Packet)> onreply, SendOptions opts) {
  CHECK_C;
  auto* impl = c_impl<RpcClientImpl>(&c);

  a0_packet_callback_t c_onreply;
  a0_rpc_client_send_options_t c_opts;

  std::string pkt_id(pkt.id());

  RpcClientSendImpl* send_impl = new RpcClientSendImpl{impl, pkt_id, opts, std::move(onreply)};

  {
    std::unique_lock<std::mutex> lk{impl->mtx};
    impl->send_impl_memory[pkt_id].reset(send_impl);

    c_onreply = {
        .user_data = send_impl,
        .fn = [](void* user_data, a0_packet_t resp) {
          auto* send_impl = (RpcClientSendImpl*)user_data;
          auto* impl = send_impl->impl;

          a0_packet_header_t req_id_hdr;

          a0_packet_header_iterator_t hdr_iter;
          a0_packet_header_iterator_init(&hdr_iter, &resp);
          if (a0_packet_header_iterator_next_match(&hdr_iter, "a0_req_id", &req_id_hdr)) {
            return;
          }

          std::function<void(Packet)> onreply;
          {
            std::unique_lock<std::mutex> lk{impl->mtx};
            onreply = std::move(send_impl->onreply);
          }
          if (!onreply) {
            return;
          }

          auto data = std::make_shared<std::vector<uint8_t>>();
          std::swap(*data, impl->data);
          onreply(Packet(resp, [data](a0_packet_t*) {}));
        }
    };

    c_opts = install_copts(send_impl);
  }

  check(a0_rpc_client_send_opts(&*c, *pkt.c, c_onreply, c_opts));
}

std::future<Packet> RpcClient::send(Packet pkt, SendOptions opts) {
  auto promise = std::make_shared<std::promise<Packet>>();
  auto onreply = [promise](Packet reply) {
    promise->set_value(reply);
  };

  auto user_ontimeout = opts.ontimeout;
  opts.ontimeout = [user_ontimeout, promise](SendOptions* opts) {
    Action action = Action::CANCEL;
    if (user_ontimeout) {
      action = user_ontimeout(opts);
      // TODO: What if action is not cancel?
    }
    try {
      check(A0_ERR_TIMEDOUT);
    } catch(...) {
      promise->set_exception(std::current_exception());
    }
    return action;
  };

  send(pkt, onreply, opts);
  return promise->get_future();
}

void RpcClient::cancel(string_view id) {
  CHECK_C;
  check(a0_rpc_client_cancel(&*c, id.data()));
}

Deadman RpcClient::server_deadman() {
  CHECK_C;
  auto save = c;
  return make_cpp<Deadman>(
      [&](a0_deadman_t* deadman) {
        return a0_rpc_client_server_deadman(&*c, &deadman);
      },
      [save](a0_deadman_t*) {});
}

uint64_t RpcClient::server_wait_up() {
  return server_deadman().wait_taken();
}

uint64_t RpcClient::server_wait_up(TimeMono timeout) {
  return server_deadman().wait_taken(timeout);
}

void RpcClient::server_wait_down(uint64_t tkn) {
  return server_deadman().wait_released(tkn);
}

void RpcClient::server_wait_down(uint64_t tkn, TimeMono timeout) {
  return server_deadman().wait_released(tkn, timeout);
}

Deadman::State RpcClient::server_state() {
  return server_deadman().state();
}

}  // namespace a0
