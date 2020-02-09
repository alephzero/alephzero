#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/stream.h>

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "macros.h"
#include "packet_tools.h"
#include "stream_tools.hpp"
#include "sync.hpp"

///////////////////
//  Prpc Common  //
///////////////////

static const char kPrpcType[] = "a0_prpc_type";
static const char kPrpcTypeConnect[] = "connect";
static const char kPrpcTypeProgress[] = "progress";
static const char kPrpcTypeComplete[] = "complete";
static const char kPrpcTypeCancel[] = "cancel";

static const char kPrpcConnId[] = "a0_conn_id";

A0_STATIC_INLINE
a0_stream_protocol_t protocol_info() {
  static a0_stream_protocol_t protocol = []() {
    static const char kProtocolName[] = "a0_prpc";

    a0_stream_protocol_t p;
    p.name.size = sizeof(kProtocolName);
    p.name.ptr = (uint8_t*)kProtocolName;

    p.major_version = 0;
    p.minor_version = 1;
    p.patch_version = 0;

    p.metadata_size = 0;

    return p;
  }();

  return protocol;
}

//////////////
//  Server  //
//////////////

struct a0_prpc_server_impl_s {
  a0::stream_thread worker;

  bool started_empty{false};
};

errno_t a0_prpc_server_init(a0_prpc_server_t* server,
                            a0_buf_t arena,
                            a0_alloc_t alloc,
                            a0_prpc_connection_callback_t onconnect,
                            a0_packet_id_callback_t oncancel) {
  server->_impl = new a0_prpc_server_impl_t;

  auto on_stream_init = [server](a0_locked_stream_t slk, a0_stream_init_status_t) -> errno_t {
    // TODO: check stream valid?

    a0_stream_empty(slk, &server->_impl->started_empty);
    if (!server->_impl->started_empty) {
      a0_stream_jump_tail(slk);
    }

    return A0_OK;
  };

  auto handle_pkt = [alloc, onconnect, oncancel, server](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);

    a0_packet_t pkt;
    alloc.fn(alloc.user_data, frame.hdr.data_size, &pkt);
    memcpy(pkt.ptr, frame.data, pkt.size);

    a0_unlock_stream(slk);

    const char* prpc_type;
    a0_packet_find_header(pkt, kPrpcType, 0, &prpc_type, nullptr);
    if (!strcmp(prpc_type, kPrpcTypeConnect)) {
      onconnect.fn(onconnect.user_data,
                   a0_prpc_connection_t{
                       .server = server,
                       .pkt = pkt,
                   });
    } else if (!strcmp(prpc_type, kPrpcTypeCancel)) {
      if (oncancel.fn) {
        a0_buf_t payload;
        a0_packet_payload(pkt, &payload);
        a0_packet_id_t id;
        memcpy(id, payload.ptr, payload.size);
        oncancel.fn(oncancel.user_data, id);
      }
    }

    a0_lock_stream(slk.stream, &slk);
  };

  auto on_stream_nonempty = [server, handle_pkt](a0_locked_stream_t slk) {
    if (server->_impl->started_empty) {
      a0_stream_jump_head(slk);
      handle_pkt(slk);
    }
  };

  auto on_stream_hasnext = [handle_pkt](a0_locked_stream_t slk) {
    a0_stream_next(slk);
    handle_pkt(slk);
  };

  return server->_impl->worker.init(arena,
                                    protocol_info(),
                                    on_stream_init,
                                    on_stream_nonempty,
                                    on_stream_hasnext);
}

errno_t a0_prpc_server_async_close(a0_prpc_server_t* server, a0_callback_t onclose) {
  if (!server || !server->_impl) {
    return ESHUTDOWN;
  }

  server->_impl->worker.async_close([server, onclose]() {
    delete server->_impl;
    server->_impl = nullptr;
    if (onclose.fn) {
      onclose.fn(onclose.user_data);
    }
  });

  return A0_OK;
}

errno_t a0_prpc_server_close(a0_prpc_server_t* server) {
  if (!server || !server->_impl) {
    return ESHUTDOWN;
  }

  server->_impl->worker.await_close();
  delete server->_impl;
  server->_impl = nullptr;

  return A0_OK;
}

errno_t a0_prpc_send(a0_prpc_connection_t conn, const a0_packet_t prog_pkt, bool done) {
  if (!conn.server || !conn.server->_impl) {
    return ESHUTDOWN;
  }

  a0_packet_id_t conn_id;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_id(conn.pkt, &conn_id));

  a0_packet_id_t prog_id;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_id(prog_pkt, &prog_id));

  // TODO: Is there a better way?
  if (!strcmp(conn_id, prog_id)) {
    return EINVAL;
  }

  char mono_str[20];
  char wall_str[36];
  a0::time_strings(mono_str, wall_str);

  constexpr size_t num_extra_headers = 5;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {kPrpcType, done ? kPrpcTypeComplete : kPrpcTypeProgress},
      {kPrpcConnId, conn_id},
      {kMonoTime, mono_str},
      {kWallTime, wall_str},
      {a0_packet_dep_key(), conn_id},
  };

  // TODO: Add sequence numbers.

  // TODO: Check impl, worker, state, and stream are still valid?
  a0::sync_stream_t ss{&conn.server->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers({extra_headers, num_extra_headers, nullptr},
                                           prog_pkt,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    return a0_stream_commit(slk);
  });
}

//////////////
//  Client  //
//////////////

namespace {

struct prpc_state {
  std::unordered_map<std::string, a0_prpc_callback_t> outstanding;
  bool closing{false};
};

}  // namespace

struct a0_prpc_client_impl_s {
  a0::stream_thread worker;

  std::shared_ptr<a0::sync<prpc_state>> state;
  bool started_empty{false};
};

errno_t a0_prpc_client_init(a0_prpc_client_t* client, a0_buf_t arena, a0_alloc_t alloc) {
  client->_impl = new a0_prpc_client_impl_t;
  client->_impl->state = std::make_shared<a0::sync<prpc_state>>();

  auto on_stream_init = [client](a0_locked_stream_t slk, a0_stream_init_status_t) -> errno_t {
    // TODO: check stream valid?

    a0_stream_empty(slk, &client->_impl->started_empty);
    if (!client->_impl->started_empty) {
      a0_stream_jump_tail(slk);
    }

    return A0_OK;
  };

  std::weak_ptr<a0::sync<prpc_state>> weak_state = client->_impl->state;
  auto handle_pkt = [alloc, weak_state](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);

    const char* prpc_type;
    a0_packet_find_header(a0::buf(frame), kPrpcType, 0, &prpc_type, nullptr);

    bool is_progress = !strcmp(prpc_type, kPrpcTypeProgress);
    bool is_complete = !strcmp(prpc_type, kPrpcTypeComplete);
    if (!is_progress && !is_complete) {
      return;
    }

    // Note: This allocs for each packet, not just ones this rpc client cares about.
    // TODO: Is there a clean way to fix that?
    a0_packet_t pkt;
    alloc.fn(alloc.user_data, frame.hdr.data_size, &pkt);
    memcpy(pkt.ptr, frame.data, pkt.size);

    a0_unlock_stream(slk);

    auto strong_state = weak_state.lock();
    if (strong_state) {
      auto callback = strong_state->with_lock([&](prpc_state* state) -> a0_prpc_callback_t {
        if (state->closing) {
          return {nullptr, nullptr};
        }

        const char* conn_id;
        a0_packet_find_header(pkt, kPrpcConnId, 0, &conn_id, nullptr);

        if (state->outstanding.count(conn_id)) {
          auto callback = state->outstanding[conn_id];
          if (is_complete) {
            state->outstanding.erase(conn_id);
          }
          return callback;
        }

        return {nullptr, nullptr};
      });

      if (callback.fn) {
        callback.fn(callback.user_data, pkt, is_complete);
      }
    }

    a0_lock_stream(slk.stream, &slk);
  };

  auto on_stream_nonempty = [client, handle_pkt](a0_locked_stream_t slk) {
    if (client->_impl->started_empty) {
      a0_stream_jump_head(slk);
    }
    handle_pkt(slk);
  };

  auto on_stream_hasnext = [handle_pkt](a0_locked_stream_t slk) {
    a0_stream_next(slk);
    handle_pkt(slk);
  };

  return client->_impl->worker.init(arena,
                                    protocol_info(),
                                    on_stream_init,
                                    on_stream_nonempty,
                                    on_stream_hasnext);
}

errno_t a0_prpc_client_async_close(a0_prpc_client_t* client, a0_callback_t onclose) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  client->_impl->state->with_lock([&](prpc_state* state) {
    state->closing = true;
  });

  client->_impl->worker.async_close([client, onclose]() {
    delete client->_impl;
    client->_impl = nullptr;
    if (onclose.fn) {
      onclose.fn(onclose.user_data);
    }
  });

  return A0_OK;
}

errno_t a0_prpc_client_close(a0_prpc_client_t* client) {
  if (!client || !client->_impl) {
    return ESHUTDOWN;
  }

  client->_impl->worker.await_close();
  delete client->_impl;
  client->_impl = nullptr;

  return A0_OK;
}

errno_t a0_prpc_connect(a0_prpc_client_t* client,
                        const a0_packet_t pkt,
                        a0_prpc_callback_t callback) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  a0_packet_id_t id;
  a0_packet_id(pkt, &id);
  client->_impl->state->with_lock([&](prpc_state* state) {
    state->outstanding[id] = callback;
  });

  char mono_str[20];
  char wall_str[36];
  a0::time_strings(mono_str, wall_str);

  constexpr size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {kPrpcType, kPrpcTypeConnect},
      {kMonoTime, mono_str},
      {kWallTime, wall_str},
  };

  // TODO: Add sequence numbers.

  // TODO: Check impl and state still valid?
  a0::sync_stream_t ss{&client->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers({extra_headers, num_extra_headers, nullptr},
                                           pkt,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_commit(slk));
    return A0_OK;
  });
}

errno_t a0_prpc_cancel(a0_prpc_client_t* client, const a0_packet_id_t conn_id) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  client->_impl->state->with_lock([&](prpc_state* state) {
    state->outstanding.erase(conn_id);
  });

  char mono_str[20];
  char wall_str[36];
  a0::time_strings(mono_str, wall_str);

  constexpr size_t num_headers = 4;
  a0_packet_header_t headers[num_headers] = {
      {kPrpcType, kPrpcTypeCancel},
      {kMonoTime, mono_str},
      {kWallTime, wall_str},
      {a0_packet_dep_key(), conn_id},
  };

  // TODO: Add sequence numbers.

  // TODO: Check impl and state still valid?
  a0::sync_stream_t ss{&client->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    A0_INTERNAL_RETURN_ERR_ON_ERR(
        a0_packet_build({{headers, num_headers, nullptr},
                         a0_buf_t{.ptr = (uint8_t*)conn_id, .size = sizeof(a0_packet_id_t)}},
                        a0::stream_allocator(&slk),
                        nullptr));
    return a0_stream_commit(slk);
  });
}
