#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/prpc.h>
#include <a0/prpc.hpp>
#include <a0/string_view.hpp>
#include <a0/uuid.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "c_wrap.hpp"
#include "file_opts.hpp"

namespace a0 {

namespace {

struct PrpcServerRequestImpl {
  std::vector<uint8_t> data;
};

}  // namespace

PrpcServer PrpcConnection::server() {
  // Note: this does not extend the server lifetime.
  return cpp_wrap<PrpcServer>(*c->server);
}

Packet PrpcConnection::pkt() {
  CHECK_C;
  auto save = c;
  return Packet(c->pkt, [save](a0_packet_t*) {});
}

void PrpcConnection::send(Packet pkt, bool done) {
  CHECK_C;
  check(a0_prpc_server_send(*c, *pkt.c, done));
}

namespace {

struct PrpcServerImpl {
  std::vector<uint8_t> data;
  std::function<void(PrpcConnection)> onconnect;
  std::function<void(string_view)> oncancel;
};

}  // namespace

PrpcServer::PrpcServer(
    std::function<void(PrpcConnection)> onconnect,
    std::function<void(string_view)> oncancel)
    : PrpcServer(PrpcTopic{}, std::move(onconnect), std::move(oncancel)) {}

PrpcServer::PrpcServer(
    PrpcTopic topic,
    std::function<void(PrpcConnection)> onconnect,
    std::function<void(string_view /* id */)> oncancel) {
  set_c_impl<PrpcServerImpl>(
      &c,
      [&](a0_prpc_server_t* c, PrpcServerImpl* impl) {
        impl->onconnect = std::move(onconnect);
        impl->oncancel = std::move(oncancel);

        auto cfo = c_fileopts(topic.file_opts);
        a0_prpc_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (PrpcServerImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_prpc_connection_callback_t c_onconnect = {
            .user_data = impl,
            .fn = [](void* user_data, a0_prpc_connection_t conn) {
              auto* impl = (PrpcServerImpl*)user_data;

              PrpcConnection cpp_conn = make_cpp_impl<PrpcConnection, PrpcServerRequestImpl>(
                  [&](a0_prpc_connection_t* c_conn, PrpcServerRequestImpl* conn_impl) {
                    std::swap(impl->data, conn_impl->data);
                    *c_conn = conn;
                    return A0_OK;
                  });

              impl->onconnect(cpp_conn);
            }};

        a0_packet_id_callback_t c_oncancel = {
            .user_data = impl,
            .fn = [](void* user_data, a0_uuid_t id) {
              auto* impl = (PrpcServerImpl*)user_data;
              impl->oncancel(id);
            }};

        return a0_prpc_server_init(c, c_topic, alloc, c_onconnect, c_oncancel);
      },
      [](a0_prpc_server_t* c, PrpcServerImpl*) {
        a0_prpc_server_close(c);
      });
}

namespace {

struct PrpcClientImpl {
  std::vector<uint8_t> data;

  std::unordered_map<std::string, std::function<void(Packet, bool /* done */)>> user_onprogress;
  std::mutex user_onprogress_mu;
};

}  // namespace

PrpcClient::PrpcClient()
    : PrpcClient(PrpcTopic{}) {}

PrpcClient::PrpcClient(PrpcTopic topic) {
  set_c_impl<PrpcClientImpl>(
      &c,
      [&](a0_prpc_client_t* c, PrpcClientImpl* impl) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_prpc_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (PrpcClientImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        return a0_prpc_client_init(c, c_topic, alloc);
      },
      [](a0_prpc_client_t* c, PrpcClientImpl*) {
        a0_prpc_client_close(c);
      });
}

void PrpcClient::connect(Packet pkt, std::function<void(Packet, bool /* done */)> onprogress) {
  CHECK_C;

  a0_prpc_progress_callback_t c_onprogress = A0_EMPTY;
  if (onprogress) {
    auto* impl = c_impl<PrpcClientImpl>(&c);
    {
      std::unique_lock<std::mutex> lk{impl->user_onprogress_mu};
      impl->user_onprogress[std::string(pkt.id())] = std::move(onprogress);
    }

    c_onprogress = {
        .user_data = impl,
        .fn = [](void* user_data, a0_packet_t prog, bool done) {
          auto* impl = (PrpcClientImpl*)user_data;

          a0_packet_header_t conn_id_hdr;

          a0_packet_header_iterator_t hdr_iter;
          a0_packet_header_iterator_init(&hdr_iter, &prog);
          if (a0_packet_header_iterator_next_match(&hdr_iter, "a0_conn_id", &conn_id_hdr)) {
            return;
          }

          std::function<void(Packet, bool)> onprogress;
          {
            std::unique_lock<std::mutex> lk{impl->user_onprogress_mu};
            auto iter = impl->user_onprogress.find(conn_id_hdr.val);
            onprogress = iter->second;
            if (done) {
              impl->user_onprogress.erase(iter);
            }
          }

          onprogress(Packet(prog, nullptr), done);
        },
    };
  }

  check(a0_prpc_client_connect(&*c, *pkt.c, c_onprogress));
}

void PrpcClient::cancel(string_view id) {
  CHECK_C;
  check(a0_prpc_client_cancel(&*c, id.data()));
}

}  // namespace a0
