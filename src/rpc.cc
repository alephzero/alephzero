#include <a0/rpc.h>

#include <string.h>

#include <condition_variable>
#include <unordered_map>

#include "macros.h"
#include "packet_tools.h"
#include "stream_tools.hh"

//////////////////
//  Rpc Common  //
//////////////////

static const char kRpcType[] = "a0_rpc_type";
static const char kRpcTypeRequest[] = "request";
static const char kRpcTypeResponse[] = "response";
static const char kRpcTypeCancel[] = "cancel";

static const char kRequestId[] = "a0_req_id";

A0_STATIC_INLINE
a0_stream_protocol_t protocol_info() {
  static a0_stream_protocol_t protocol = []() {
    static const char kProtocolName[] = "a0_rpc";

    a0_stream_protocol_t p;
    p.name.size = strlen(kProtocolName);
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

struct a0_rpc_server_impl_s {
  a0::stream_thread worker;

  bool started_empty{false};
};

errno_t a0_rpc_server_init(a0_rpc_server_t* server,
                           a0_shmobj_t shmobj,
                           a0_alloc_t alloc,
                           a0_packet_callback_t onrequest,
                           a0_packet_id_callback_t oncancel) {
  server->_impl = new a0_rpc_server_impl_t;

  auto on_stream_init = [server](a0_locked_stream_t slk, a0_stream_init_status_t) -> errno_t {
    // TODO: check stream valid?

    a0_stream_empty(slk, &server->_impl->started_empty);
    if (!server->_impl->started_empty) {
      a0_stream_jump_tail(slk);
    }

    return A0_OK;
  };

  auto handle_pkt = [alloc, onrequest, oncancel](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);

    a0_packet_t pkt;
    alloc.fn(alloc.user_data, frame.hdr.data_size, &pkt);
    memcpy(pkt.ptr, frame.data, pkt.size);

    a0_unlock_stream(slk);

    const char* rpc_type;
    a0_packet_find_header(pkt, kRpcType, &rpc_type);
    if (!strcmp(rpc_type, kRpcTypeRequest)) {
      onrequest.fn(onrequest.user_data, pkt);
    } else if (!strcmp(rpc_type, kRpcTypeCancel)) {
      if (oncancel.fn) {
        a0_packet_id_t id;
        a0_packet_id(pkt, &id);
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

  return server->_impl->worker.init(shmobj,
                                    protocol_info(),
                                    on_stream_init,
                                    on_stream_nonempty,
                                    on_stream_hasnext);
}

errno_t a0_rpc_server_async_close(a0_rpc_server_t* server, a0_callback_t onclose) {
  if (!server || !server->_impl) {
    return ESHUTDOWN;
  }

  auto worker_ = server->_impl->worker;
  delete server->_impl;
  server->_impl = nullptr;

  worker_.async_close([onclose]() {
    if (onclose.fn) {
      onclose.fn(onclose.user_data);
    }
  });

  return A0_OK;
}

errno_t a0_rpc_server_close(a0_rpc_server_t* server) {
  if (!server || !server->_impl) {
    return ESHUTDOWN;
  }

  auto worker_ = server->_impl->worker;
  delete server->_impl;
  server->_impl = nullptr;
  worker_.await_close();

  return A0_OK;
}

errno_t a0_rpc_reply(a0_rpc_server_t* server, const a0_packet_id_t req_id, const a0_packet_t resp) {
  if (!server->_impl) {
    return ESHUTDOWN;
  }

  a0_packet_id_t resp_id;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_id(resp, &resp_id));

  // TODO: Is there a better way?
  if (!strcmp(req_id, resp_id)) {
    return EINVAL;
  }

  constexpr size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[num_extra_headers];

  extra_headers[0].key = kRequestId;
  extra_headers[0].val = req_id;

  extra_headers[1].key = kRpcType;
  extra_headers[1].val = kRpcTypeResponse;

  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  auto clock_str = std::to_string(clock_val);
  extra_headers[2].key = kSendClock;
  extra_headers[2].val = clock_str.c_str();

  // TODO: Add sequence numbers.

  // TODO: Check impl, worker, state, and stream are still valid?
  a0::sync_stream_t ss{&server->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers(num_extra_headers,
                                           extra_headers,
                                           resp,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    return a0_stream_commit(slk);
  });
}

//////////////
//  Client  //
//////////////

namespace {

struct rpc_state {
  std::unordered_map<std::string, a0_packet_callback_t> outstanding;
  bool closing{false};
  std::mutex mu;
};

}  // namespace

struct a0_rpc_client_impl_s {
  a0::stream_thread worker;

  std::shared_ptr<rpc_state> state;
  bool started_empty{false};
};

errno_t a0_rpc_client_init(a0_rpc_client_t* client, a0_shmobj_t shmobj, a0_alloc_t alloc) {
  client->_impl = new a0_rpc_client_impl_t;
  client->_impl->state = std::make_shared<rpc_state>();

  auto on_stream_init = [client](a0_locked_stream_t slk, a0_stream_init_status_t) -> errno_t {
    // TODO: check stream valid?

    a0_stream_empty(slk, &client->_impl->started_empty);
    if (!client->_impl->started_empty) {
      a0_stream_jump_tail(slk);
    }

    return A0_OK;
  };

  std::weak_ptr<rpc_state> weak_state = client->_impl->state;
  auto handle_pkt = [alloc, weak_state](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);

    const char* rpc_type;
    a0_packet_find_header(a0::buf(frame), kRpcType, &rpc_type);

    if (strcmp(rpc_type, kRpcTypeResponse)) {
      return;
    }

    // Note: This allocs for each packet, not just ones this rpc client cares about.
    // TODO: Is there a clean way to fix that?
    a0_packet_t pkt;
    alloc.fn(alloc.user_data, frame.hdr.data_size, &pkt);
    memcpy(pkt.ptr, frame.data, pkt.size);

    a0_unlock_stream(slk);

    auto strong_state = weak_state.lock();
    [&]() {
      if (!strong_state) {
        return;
      }
      std::unique_lock<std::mutex> lk{strong_state->mu};
      if (strong_state->closing) {
        return;
      }

      const char* req_id;
      a0_packet_find_header(pkt, kRequestId, &req_id);

      if (strong_state->outstanding.count(req_id)) {
        auto callback = strong_state->outstanding[req_id];
        strong_state->outstanding.erase(req_id);

        lk.unlock();
        callback.fn(callback.user_data, pkt);
      }
    }();

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

  return client->_impl->worker.init(shmobj,
                                    protocol_info(),
                                    on_stream_init,
                                    on_stream_nonempty,
                                    on_stream_hasnext);
}

errno_t a0_rpc_client_async_close(a0_rpc_client_t* client, a0_callback_t onclose) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  {
    std::unique_lock<std::mutex> lk{client->_impl->state->mu};
    client->_impl->state->closing = true;
  }

  auto worker_ = client->_impl->worker;
  delete client->_impl;
  client->_impl = nullptr;

  worker_.async_close([onclose]() {
    if (onclose.fn) {
      onclose.fn(onclose.user_data);
    }
  });

  return A0_OK;
}

errno_t a0_rpc_client_close(a0_rpc_client_t* client) {
  if (!client || !client->_impl) {
    return ESHUTDOWN;
  }

  auto worker_ = client->_impl->worker;
  delete client->_impl;
  client->_impl = nullptr;
  worker_.await_close();

  return A0_OK;
}

errno_t a0_rpc_send(a0_rpc_client_t* client, const a0_packet_t pkt, a0_packet_callback_t callback) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  a0_packet_id_t id;
  a0_packet_id(pkt, &id);
  {
    std::unique_lock<std::mutex> lk{client->_impl->state->mu};
    client->_impl->state->outstanding[id] = callback;
  }

  constexpr size_t num_extra_headers = 2;
  a0_packet_header_t extra_headers[num_extra_headers];

  extra_headers[0].key = kRpcType;
  extra_headers[0].val = kRpcTypeRequest;

  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  std::string clock_str = std::to_string(clock_val);
  extra_headers[1].key = kSendClock;
  extra_headers[1].val = clock_str.c_str();

  // TODO: Add sequence numbers.

  // TODO: Check impl and state still valid?
  a0::sync_stream_t ss{&client->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers(num_extra_headers,
                                           extra_headers,
                                           pkt,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_commit(slk));
    return A0_OK;
  });
}

errno_t a0_rpc_cancel(a0_rpc_client_t* client, const a0_packet_id_t req_id) {
  if (!client->_impl || !client->_impl->state) {
    return ESHUTDOWN;
  }

  {
    std::unique_lock<std::mutex> lk{client->_impl->state->mu};
    client->_impl->state->outstanding.erase(req_id);
  }

  constexpr size_t num_headers = 3;
  a0_packet_header_t headers[num_headers];

  headers[0].key = kRpcType;
  headers[0].val = kRpcTypeCancel;

  headers[1].key = kRequestId;
  headers[1].val = req_id;

  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  std::string clock_str = std::to_string(clock_val);
  headers[2].key = kSendClock;
  headers[2].val = clock_str.c_str();

  // TODO: Add sequence numbers.

  // TODO: Check impl and state still valid?
  a0::sync_stream_t ss{&client->_impl->worker.state->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_packet_build(num_headers,
                                                  headers,
                                                  a0_buf_t{.ptr = nullptr, .size = 0},
                                                  a0::stream_allocator(&slk),
                                                  nullptr));
    return a0_stream_commit(slk);
  });
}
