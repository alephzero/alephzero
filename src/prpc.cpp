#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/uuid.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "sync.hpp"
#include "transport_tools.hpp"

///////////////////
//  Prpc Common  //
///////////////////

static constexpr std::string_view PRPC_TYPE = "a0_prpc_type";
static constexpr std::string_view PRPC_TYPE_CONNECT = "connect";
static constexpr std::string_view PRPC_TYPE_PROGRESS = "progress";
static constexpr std::string_view PRPC_TYPE_COMPLETE = "complete";
static constexpr std::string_view PRPC_TYPE_CANCEL = "cancel";

static constexpr std::string_view PRPC_CONN_ID = "a0_conn_id";

//////////////
//  Server  //
//////////////

struct a0_prpc_server_impl_s {
  a0_subscriber_t conn_reader;
  a0_publisher_t prog_writer;

  a0_prpc_connection_callback_t onconnect;
  a0_packet_id_callback_t oncancel;
};

errno_t a0_prpc_server_init(a0_prpc_server_t* server,
                            a0_arena_t arena,
                            a0_alloc_t alloc,
                            a0_prpc_connection_callback_t onconnect,
                            a0_packet_id_callback_t oncancel) {
  server->_impl = new a0_prpc_server_impl_t;

  server->_impl->onconnect = onconnect;
  server->_impl->oncancel = oncancel;
  a0_packet_callback_t onpacket = {
      .user_data = server,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* server = (a0_prpc_server_t*)user_data;
            auto* impl = server->_impl;

            auto prpc_type = a0::find_header(pkt, PRPC_TYPE);
            if (prpc_type == PRPC_TYPE_CONNECT) {
              impl->onconnect.fn(impl->onconnect.user_data,
                                 a0_prpc_connection_t{
                                     .server = server,
                                     .pkt = pkt,
                                 });
            } else if (bool(impl->oncancel.fn) && prpc_type == PRPC_TYPE_CANCEL) {
              impl->oncancel.fn(impl->oncancel.user_data, (char*)pkt.payload.ptr);
            }
          },
  };

  a0_publisher_init(&server->_impl->prog_writer, arena);
  a0_subscriber_init(&server->_impl->conn_reader,
                     arena,
                     alloc,
                     A0_INIT_AWAIT_NEW,
                     A0_ITER_NEXT,
                     onpacket);

  return A0_OK;
}

errno_t a0_prpc_server_async_close(a0_prpc_server_t* server, a0_callback_t onclose) {
  if (!server->_impl) {
    return ESHUTDOWN;
  }

  struct data_t {
    a0_prpc_server_t* server;
    a0_callback_t onclose;
  };

  a0_callback_t wrapped_onclose = {
      .user_data = new data_t{server, onclose},
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            a0_publisher_close(&data->server->_impl->prog_writer);
            delete data->server->_impl;
            data->server->_impl = nullptr;
            if (data->onclose.fn) {
              data->onclose.fn(data->onclose.user_data);
            }
            delete data;
          },
  };

  return a0_subscriber_async_close(&server->_impl->conn_reader, wrapped_onclose);
}

errno_t a0_prpc_server_close(a0_prpc_server_t* server) {
  if (!server->_impl) {
    return ESHUTDOWN;
  }

  a0_subscriber_close(&server->_impl->conn_reader);
  a0_publisher_close(&server->_impl->prog_writer);
  delete server->_impl;
  server->_impl = nullptr;

  return A0_OK;
}

errno_t a0_prpc_send(a0_prpc_connection_t conn, const a0_packet_t prog, bool done) {
  if (!conn.server->_impl) {
    return ESHUTDOWN;
  }

  // TODO(lshamis): Is there a better way?
  if (!strcmp(conn.pkt.id, prog.id)) {
    return EINVAL;
  }

  constexpr size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {PRPC_TYPE.data(), done ? PRPC_TYPE_COMPLETE.data() : PRPC_TYPE_PROGRESS.data()},
      {PRPC_CONN_ID.data(), conn.pkt.id},
      {A0_PACKET_DEP_KEY, conn.pkt.id},
  };

  a0_packet_t full_prog = prog;
  full_prog.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&prog.headers_block,
  };

  return a0_pub(&conn.server->_impl->prog_writer, full_prog);
}

//////////////
//  Client  //
//////////////

struct a0_prpc_client_impl_s {
  a0_publisher_t conn_writer;
  a0_subscriber_t prog_reader;

  a0::sync<std::unordered_map<std::string, a0_prpc_callback_t>> outstanding;
};

errno_t a0_prpc_client_init(a0_prpc_client_t* client, a0_arena_t arena, a0_alloc_t alloc) {
  client->_impl = new a0_prpc_client_impl_t;

  a0_packet_callback_t onpacket = {
      .user_data = client,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* client = (a0_prpc_client_t*)user_data;
            auto* impl = client->_impl;

            auto prpc_type = a0::find_header(pkt, PRPC_TYPE);

            bool is_progress = (prpc_type == PRPC_TYPE_PROGRESS);
            bool is_complete = (prpc_type == PRPC_TYPE_COMPLETE);
            if (!is_progress && !is_complete) {
              return;
            }

            auto conn_id = std::string(a0::find_header(pkt, PRPC_CONN_ID));

            auto callback =
                impl->outstanding.with_lock([&](auto* outstanding_) -> a0_prpc_callback_t {
                  if (outstanding_->count(conn_id)) {
                    a0_prpc_callback_t callback = (*outstanding_)[conn_id];
                    if (is_complete) {
                      outstanding_->erase(conn_id);
                    }
                    return callback;
                  }
                  return {};
                });

            if (callback.fn) {
              callback.fn(callback.user_data, pkt, is_complete);
            }
          },
  };

  a0_publisher_init(&client->_impl->conn_writer, arena);
  a0_subscriber_init(&client->_impl->prog_reader,
                     arena,
                     alloc,
                     A0_INIT_AWAIT_NEW,
                     A0_ITER_NEXT,
                     onpacket);

  return A0_OK;
}

errno_t a0_prpc_client_async_close(a0_prpc_client_t* client, a0_callback_t onclose) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  struct data_t {
    a0_prpc_client_t* client;
    a0_callback_t onclose;
  };

  a0_callback_t wrapped_onclose = {
      .user_data = new data_t{client, onclose},
      .fn =
          [](void* user_data) {
            auto* data = (data_t*)user_data;
            a0_publisher_close(&data->client->_impl->conn_writer);
            delete data->client->_impl;
            data->client->_impl = nullptr;
            if (data->onclose.fn) {
              data->onclose.fn(data->onclose.user_data);
            }
            delete data;
          },
  };

  return a0_subscriber_async_close(&client->_impl->prog_reader, wrapped_onclose);
}

errno_t a0_prpc_client_close(a0_prpc_client_t* client) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  a0_subscriber_close(&client->_impl->prog_reader);
  a0_publisher_close(&client->_impl->conn_writer);
  delete client->_impl;
  client->_impl = nullptr;

  return A0_OK;
}

errno_t a0_prpc_connect(a0_prpc_client_t* client,
                        const a0_packet_t pkt,
                        a0_prpc_callback_t callback) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  client->_impl->outstanding.with_lock([&](auto* outstanding_) {
    (*outstanding_)[pkt.id] = callback;
  });

  constexpr size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {PRPC_TYPE.data(), PRPC_TYPE_CONNECT.data()},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_pub(&client->_impl->conn_writer, full_pkt);
}

errno_t a0_prpc_cancel(a0_prpc_client_t* client, const a0_uuid_t conn_id) {
  if (!client->_impl) {
    return ESHUTDOWN;
  }

  client->_impl->outstanding.with_lock([&](auto* outstanding_) {
    outstanding_->erase(conn_id);
  });

  constexpr size_t num_headers = 2;
  a0_packet_header_t headers[num_headers] = {
      {PRPC_TYPE.data(), PRPC_TYPE_CANCEL.data()},
      {A0_PACKET_DEP_KEY, conn_id},
  };

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  pkt.headers_block = {
      .headers = headers,
      .size = num_headers,
      .next_block = nullptr,
  };
  pkt.payload = {
      .ptr = (uint8_t*)conn_id,
      .size = sizeof(a0_uuid_t),
  };

  return a0_pub(&client->_impl->conn_writer, pkt);
}
