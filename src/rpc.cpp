#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/rpc.h>
#include <a0/uuid.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "sync.hpp"
#include "transport_tools.hpp"

//////////////////
//  Rpc Common  //
//////////////////

static constexpr std::string_view RPC_TYPE = "a0_rpc_type";
static constexpr std::string_view RPC_TYPE_REQUEST = "request";
static constexpr std::string_view RPC_TYPE_RESPONSE = "response";
static constexpr std::string_view RPC_TYPE_CANCEL = "cancel";

static constexpr std::string_view REQUEST_ID = "a0_req_id";

//////////////
//  Server  //
//////////////

struct a0_rpc_server_impl_s {
  a0_subscriber_t req_reader;
  a0_publisher_t resp_writer;

  a0_rpc_request_callback_t onrequest;
  a0_packet_id_callback_t oncancel;
};

errno_t a0_rpc_server_init(a0_rpc_server_t* server,
                           a0_arena_t arena,
                           a0_alloc_t alloc,
                           a0_rpc_request_callback_t onrequest,
                           a0_packet_id_callback_t oncancel) {
  server->_impl = new a0_rpc_server_impl_t;

  server->_impl->onrequest = onrequest;
  server->_impl->oncancel = oncancel;
  a0_packet_callback_t onpacket = {
      .user_data = server,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* server = (a0_rpc_server_t*)user_data;
            auto* impl = server->_impl;

            auto rpc_type = a0::find_header(pkt, RPC_TYPE);
            if (rpc_type == RPC_TYPE_REQUEST) {
              impl->onrequest.fn(impl->onrequest.user_data,
                                 a0_rpc_request_t{
                                     .server = server,
                                     .pkt = pkt,
                                 });
            } else if (bool(impl->oncancel.fn) && rpc_type == RPC_TYPE_CANCEL) {
              impl->oncancel.fn(impl->oncancel.user_data, (char*)pkt.payload.ptr);
            }
          },
  };

  a0_publisher_init(&server->_impl->resp_writer, arena);
  a0_subscriber_init(&server->_impl->req_reader,
                     arena,
                     alloc,
                     A0_INIT_AWAIT_NEW,
                     A0_ITER_NEXT,
                     onpacket);

  return A0_OK;
}

errno_t a0_rpc_server_async_close(a0_rpc_server_t* server, a0_callback_t onclose) {
  if (!server->_impl) {
    return ESHUTDOWN;
  }

  struct data_t {
    a0_rpc_server_t* server;
    a0_callback_t onclose;
  };

  a0_callback_t wrapped_onclose = {
      .user_data = new data_t{server, onclose},
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            a0_publisher_close(&data->server->_impl->resp_writer);
            delete data->server->_impl;
            data->server->_impl = nullptr;
            if (data->onclose.fn) {
              data->onclose.fn(data->onclose.user_data);
            }
            delete data;
          },
  };

  return a0_subscriber_async_close(&server->_impl->req_reader, wrapped_onclose);
}

errno_t a0_rpc_server_close(a0_rpc_server_t* server) {
  if (!server->_impl) {
    return ESHUTDOWN;
  }

  a0_subscriber_close(&server->_impl->req_reader);
  a0_publisher_close(&server->_impl->resp_writer);
  delete server->_impl;
  server->_impl = nullptr;

  return A0_OK;
}

errno_t a0_rpc_reply(a0_rpc_request_t req, const a0_packet_t resp) {
  if (!req.server->_impl) {
    return ESHUTDOWN;
  }

  constexpr size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {RPC_TYPE.data(), RPC_TYPE_RESPONSE.data()},
      {REQUEST_ID.data(), req.pkt.id},
      {A0_PACKET_DEP_KEY, req.pkt.id},
  };

  a0_packet_t full_resp = resp;
  full_resp.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&resp.headers_block,
  };

  return a0_pub(&req.server->_impl->resp_writer, full_resp);
}

//////////////
//  Client  //
//////////////

struct a0_rpc_client_impl_s {
  a0_publisher_t req_writer;
  a0_subscriber_t resp_reader;

  a0::sync<std::unordered_map<std::string, a0_packet_callback_t>> outstanding;
};

errno_t a0_rpc_client_init(a0_rpc_client_t* client, a0_arena_t arena, a0_alloc_t alloc) {
  client->_impl = new a0_rpc_client_impl_t;

  a0_packet_callback_t onpacket = {
      .user_data = client,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* client = (a0_rpc_client_t*)user_data;
            auto* impl = client->_impl;

            if (a0::find_header(pkt, RPC_TYPE) != RPC_TYPE_RESPONSE) {
              return;
            }

            auto req_id = std::string(a0::find_header(pkt, REQUEST_ID));

            auto callback =
                impl->outstanding.with_lock([&](auto* outstanding_) -> a0_packet_callback_t {
                  if (outstanding_->count(req_id)) {
                    a0_packet_callback_t callback = (*outstanding_)[req_id];
                    outstanding_->erase(req_id);
                    return callback;
                  }
                  return {};
                });

            if (callback.fn) {
              callback.fn(callback.user_data, pkt);
            }
          },
  };

  a0_publisher_init(&client->_impl->req_writer, arena);
  a0_subscriber_init(&client->_impl->resp_reader,
                     arena,
                     alloc,
                     A0_INIT_AWAIT_NEW,
                     A0_ITER_NEXT,
                     onpacket);

  return A0_OK;
}

errno_t a0_rpc_client_async_close(a0_rpc_client_t* client, a0_callback_t onclose) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  struct data_t {
    a0_rpc_client_t* client;
    a0_callback_t onclose;
  };

  a0_callback_t wrapped_onclose = {
      .user_data = new data_t{client, onclose},
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            a0_publisher_close(&data->client->_impl->req_writer);
            delete data->client->_impl;
            data->client->_impl = nullptr;
            if (data->onclose.fn) {
              data->onclose.fn(data->onclose.user_data);
            }
            delete data;
          },
  };

  return a0_subscriber_async_close(&client->_impl->resp_reader, wrapped_onclose);
}

errno_t a0_rpc_client_close(a0_rpc_client_t* client) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  a0_subscriber_close(&client->_impl->resp_reader);
  a0_publisher_close(&client->_impl->req_writer);
  delete client->_impl;
  client->_impl = nullptr;

  return A0_OK;
}

errno_t a0_rpc_send(a0_rpc_client_t* client, const a0_packet_t pkt, a0_packet_callback_t callback) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  client->_impl->outstanding.with_lock([&](auto* outstanding_) {
    (*outstanding_)[pkt.id] = callback;
  });

  constexpr size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {RPC_TYPE.data(), RPC_TYPE_REQUEST.data()},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_pub(&client->_impl->req_writer, full_pkt);
}

errno_t a0_rpc_cancel(a0_rpc_client_t* client, const a0_uuid_t req_id) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  client->_impl->outstanding.with_lock([&](auto* outstanding_) {
    outstanding_->erase(req_id);
  });

  constexpr size_t num_headers = 2;
  a0_packet_header_t headers[num_headers] = {
      {RPC_TYPE.data(), RPC_TYPE_CANCEL.data()},
      {A0_PACKET_DEP_KEY, req_id},
  };

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  pkt.headers_block = {
      .headers = headers,
      .size = num_headers,
      .next_block = nullptr,
  };
  pkt.payload = {
      .ptr = (uint8_t*)req_id,
      .size = sizeof(a0_uuid_t),
  };

  return a0_pub(&client->_impl->req_writer, pkt);
}
