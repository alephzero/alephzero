#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/cmp.h>
#include <a0/deadman.h>
#include <a0/empty.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/mtx.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/rpc.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/unused.h>
#include <a0/uuid.h>
#include <a0/vec.h>
#include <a0/writer.h>

#include <alloca.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

static const char RPC_TYPE[] = "a0_rpc_type";
static const char RPC_TYPE_REQUEST[] = "request";
static const char RPC_TYPE_RESPONSE[] = "response";
static const char RPC_TYPE_CANCEL[] = "cancel";

static const char REQUEST_ID[] = "a0_req_id";

A0_STATIC_INLINE
a0_err_t a0_rpc_topic_open(a0_rpc_topic_t topic, a0_file_t* file) {
  return a0_topic_open(a0_env_topic_tmpl_rpc(), topic.name, topic.file_opts, file);
}

A0_STATIC_INLINE
a0_err_t a0_rpc_deadman_open(a0_rpc_topic_t topic, a0_deadman_t* deadman) {
  char* deadman_name = alloca(strlen(topic.name) + strlen(".rpc") + 1);
  strcpy(deadman_name, topic.name);
  strcpy(deadman_name + strlen(topic.name), ".rpc");

  a0_deadman_topic_t deadman_topic = {.name = deadman_name};
  a0_deadman_init(deadman, deadman_topic);

  return A0_OK;
}

////////////
// Server //
////////////

A0_STATIC_INLINE
void a0_rpc_server_onpacket(void* data, a0_packet_t pkt) {
  a0_rpc_server_t* server = (a0_rpc_server_t*)data;

  // Don't start processing until the deadman is acquired.
  A0_UNUSED(a0_mtx_lock(&server->_init_lock));
  bool init_complete = server->_init_complete;
  a0_mtx_unlock(&server->_init_lock);
  if (!init_complete) {
    return;
  }

  a0_packet_header_t type_hdr;
  a0_packet_header_iterator_t hdr_iter;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  if (a0_packet_header_iterator_next_match(&hdr_iter, RPC_TYPE, &type_hdr)) {
    return;
  }

  if (!strcmp(type_hdr.val, RPC_TYPE_REQUEST)) {
    server->_onrequest.fn(server->_onrequest.user_data, (a0_rpc_request_t){server, pkt});
  } else if (!strcmp(type_hdr.val, RPC_TYPE_CANCEL)) {
    if (server->_oncancel.fn) {
      a0_uuid_t uuid;
      memcpy(uuid, pkt.payload.data, sizeof(a0_uuid_t));
      server->_oncancel.fn(server->_oncancel.user_data, uuid);
    }
  }
}

a0_err_t a0_rpc_server_init(a0_rpc_server_t* server,
                            a0_rpc_topic_t topic,
                            a0_alloc_t alloc,
                            a0_rpc_server_options_t opts) {
  server->_onrequest = opts.onrequest;
  server->_oncancel = opts.oncancel;

  // Response writer must be set up before the request reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_rpc_topic_open(topic, &server->_file));

  a0_err_t err = a0_writer_init(&server->_response_writer, server->_file.arena);
  if (err) {
    a0_file_close(&server->_file);
    return err;
  }

  err = a0_writer_push(&server->_response_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&server->_response_writer);
    a0_file_close(&server->_file);
    return err;
  }

  // Prevent the reader from processing until the deadman is acquired.
  A0_UNUSED(a0_mtx_lock(&server->_init_lock));

  err = a0_reader_init(
      &server->_request_reader,
      server->_file.arena,
      alloc,
      (a0_reader_options_t){A0_INIT_AWAIT_NEW, A0_ITER_NEXT},
      (a0_packet_callback_t){
          .user_data = server,
          .fn = a0_rpc_server_onpacket,
      });
  if (err) {
    a0_writer_close(&server->_response_writer);
    a0_file_close(&server->_file);
    return err;
  }

  a0_rpc_deadman_open(topic, &server->_deadman);
  err = a0_deadman_timedtake(&server->_deadman, opts.exclusive_ownership_timeout);
  if (!a0_mtx_lock_successful(err)) {
    a0_mtx_unlock(&server->_init_lock);
    a0_reader_close(&server->_request_reader);
    a0_writer_close(&server->_response_writer);
    a0_file_close(&server->_file);
    a0_deadman_close(&server->_deadman);
    return err;
  }
  server->_init_complete = true;
  a0_mtx_unlock(&server->_init_lock);

  return A0_OK;
}

a0_err_t a0_rpc_server_close(a0_rpc_server_t* server) {
  a0_reader_close(&server->_request_reader);
  a0_writer_close(&server->_response_writer);
  a0_file_close(&server->_file);
  a0_deadman_close(&server->_deadman);
  return A0_OK;
}

a0_err_t a0_rpc_server_reply(a0_rpc_request_t req, a0_packet_t resp) {
  const size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[] = {
      {RPC_TYPE, RPC_TYPE_RESPONSE},
      {REQUEST_ID, (char*)req.pkt.id},
      {A0_DEP, (char*)req.pkt.id},
  };

  a0_packet_t full_resp = resp;
  full_resp.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&resp.headers_block,
  };

  return a0_writer_write(&req.server->_response_writer, full_resp);
}

////////////
// Client //
////////////

A0_STATIC_INLINE
a0_err_t _a0_malloc_alloc(void* user_data, size_t size, a0_buf_t* out) {
  A0_UNUSED(user_data);
  out->size = size;
  out->data = malloc(size);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t _a0_malloc_dealloc(void* user_data, a0_buf_t buf) {
  A0_UNUSED(user_data);
  free(buf.data);
  return A0_OK;
}

A0_STATIC_INLINE
a0_alloc_t _a0_malloc() {
  return (a0_alloc_t){NULL, _a0_malloc_alloc, _a0_malloc_dealloc};
}

typedef struct _a0_rpc_client_request_s {
  a0_packet_t pkt;
  a0_buf_t pkt_buf;

  a0_packet_callback_t onreply;

  a0_time_mono_t timeout;
  bool has_timeout;
  a0_callback_t ontimeout;

  a0_onreconnect_t onreconnect;
} _a0_rpc_client_request_t;

A0_STATIC_INLINE
a0_err_t a0_find_pkt_rpctype(a0_packet_t pkt, const char** out) {
  a0_packet_header_t req_id_hdr;
  a0_packet_header_iterator_t hdr_iter;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  if (!a0_packet_header_iterator_next_match(&hdr_iter, RPC_TYPE, &req_id_hdr)) {
    *out = req_id_hdr.val;
    return A0_OK;
  }
  return A0_ERR_ITER_DONE;
}

A0_STATIC_INLINE
a0_err_t a0_find_pkt_reqid(a0_packet_t pkt, a0_uuid_t** out) {
  a0_packet_header_t req_id_hdr;
  a0_packet_header_iterator_t hdr_iter;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  if (!a0_packet_header_iterator_next_match(&hdr_iter, REQUEST_ID, &req_id_hdr)) {
    *out = (a0_uuid_t*)req_id_hdr.val;
    return A0_OK;
  }
  return A0_ERR_ITER_DONE;
}

A0_STATIC_INLINE
a0_err_t _a0_outstanding_requests_pop_locked(a0_rpc_client_t* client, const a0_uuid_t reqid, _a0_rpc_client_request_t* out) {
  size_t size;
  a0_vec_size(&client->_outstanding_requests, &size);

  for (size_t i = 0; i < size; i++) {
    _a0_rpc_client_request_t* iter;
    a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);
    int cmp;
    a0_cmp_eval(A0_CMP_UUID, reqid, &iter->pkt.id, &cmp);
    if (cmp == 0) {
      a0_vec_swap_back_pop(&client->_outstanding_requests, i, out);
      return A0_OK;
    }
  }

  return A0_ERR_NOT_FOUND;
}

A0_STATIC_INLINE
a0_err_t _a0_outstanding_requests_pop(a0_rpc_client_t* client, const a0_uuid_t reqid, _a0_rpc_client_request_t* out) {
  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  a0_err_t err = _a0_outstanding_requests_pop_locked(client, reqid, out);
  a0_mtx_unlock(&client->_mtx);
  return err;
}

A0_STATIC_INLINE
void a0_rpc_client_dosend(a0_rpc_client_t* client, a0_packet_t pkt) {
  // Pre-condition, mutex is locked.

  const size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[] = {
      {RPC_TYPE, RPC_TYPE_REQUEST},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  a0_writer_write(&client->_request_writer, full_pkt);
}

A0_STATIC_INLINE
void a0_rpc_client_onpacket(void* user_data, a0_packet_t pkt) {
  a0_rpc_client_t* client = (a0_rpc_client_t*)user_data;

  const char* rpctype;
  a0_uuid_t* reqid;
  if (a0_find_pkt_rpctype(pkt, &rpctype) ||
      strcmp(rpctype, RPC_TYPE_RESPONSE) != 0 ||
      a0_find_pkt_reqid(pkt, &reqid)) {
    return;
  }

  _a0_rpc_client_request_t req;
  if (!_a0_outstanding_requests_pop(client, *reqid, &req)) {
    a0_packet_callback_call(req.onreply, pkt);
    _a0_malloc_dealloc(NULL, req.pkt_buf);
  }
}

A0_STATIC_INLINE
void* a0_rpc_client_deadman_thread(void* user_data) {
  a0_rpc_client_t* client = (a0_rpc_client_t*)user_data;

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  while (!client->_closing) {
    a0_mtx_unlock(&client->_mtx);
    uint64_t server_tkn;
    a0_err_t err = a0_deadman_wait_taken(&client->_deadman, &server_tkn);
    A0_UNUSED(a0_mtx_lock(&client->_mtx));

    if (client->_closing || err) {
      break;
    }

    size_t size;
    a0_vec_size(&client->_outstanding_requests, &size);

    for (size_t i = 0; i < size;) {
      _a0_rpc_client_request_t* iter;
      a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);

      switch (iter->onreconnect) {
      case A0_ONRECONNECT_RESEND:
        a0_rpc_client_dosend(client, iter->pkt);
        i++;
        break;
      case A0_ONRECONNECT_CANCEL:
        a0_vec_swap_back_pop(&client->_outstanding_requests, i, NULL);
        break;
      case A0_ONRECONNECT_IGNORE:
        i++;
        break;
      }
    }

    a0_mtx_unlock(&client->_mtx);
    a0_deadman_wait_released(&client->_deadman, server_tkn);
    A0_UNUSED(a0_mtx_lock(&client->_mtx));
  }
  a0_mtx_unlock(&client->_mtx);

  return NULL;
}

A0_STATIC_INLINE
void a0_rpc_client_timeout_thread_pop_expired(a0_rpc_client_t* client, a0_vec_t* expired) {
  size_t size;
  a0_vec_size(&client->_outstanding_requests, &size);

  if (!size) {
    return;
  }

  a0_time_mono_t now;
  a0_time_mono_now(&now);

  size_t i = 0;
  while (i < size) {
    _a0_rpc_client_request_t* iter;
    a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);

    bool is_old = false;

    if (iter->has_timeout) {
      a0_time_mono_less(iter->timeout, now, &is_old);
      if (is_old) {
        a0_vec_push_back(expired, iter);
        a0_vec_swap_back_pop(&client->_outstanding_requests, i, NULL);
      }
    }

    if (is_old) {
      size--;
    } else {
      i++;
    }
  }
}

A0_STATIC_INLINE
a0_time_mono_t* a0_rpc_client_timeout_thread_min_timeout(a0_rpc_client_t* client) {
  a0_time_mono_t* ret = A0_TIMEOUT_NEVER;

  size_t size;
  a0_vec_size(&client->_outstanding_requests, &size);

  for (size_t i = 0; i < size; i++) {
    _a0_rpc_client_request_t* iter;
    a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);
    if (iter->has_timeout) {
      if (!ret) {
        ret = &iter->timeout;
      } else {
        bool is_earlier;
        a0_time_mono_less(iter->timeout, *ret, &is_earlier);
        if (is_earlier) {
          ret = &iter->timeout;
        }
      }
    }
  }

  return ret;
}

A0_STATIC_INLINE
void* a0_rpc_client_timeout_thread(void* user_data) {
  a0_rpc_client_t* client = (a0_rpc_client_t*)user_data;

  a0_vec_t expired;
  a0_vec_init(&expired, sizeof(_a0_rpc_client_request_t));

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  while (!client->_closing) {
    a0_rpc_client_timeout_thread_pop_expired(client, &expired);

    a0_mtx_unlock(&client->_mtx);

    size_t expired_size;
    a0_vec_size(&expired, &expired_size);

    for (size_t i = 0; i < expired_size; i++) {
      _a0_rpc_client_request_t* iter;
      a0_vec_at(&expired, i, (void**)&iter);
      a0_callback_call(iter->ontimeout);
      _a0_malloc_dealloc(NULL, iter->pkt_buf);
    }
    a0_vec_resize(&expired, 0);

    A0_UNUSED(a0_mtx_lock(&client->_mtx));

    if (client->_closing) {
      break;
    }

    a0_time_mono_t* timeout = a0_rpc_client_timeout_thread_min_timeout(client);
    a0_cnd_timedwait(&client->_cnd, &client->_mtx, timeout);
  }
  a0_mtx_unlock(&client->_mtx);
  a0_vec_close(&expired);
  return NULL;
}

a0_err_t a0_rpc_client_init(a0_rpc_client_t* client, a0_rpc_topic_t topic, a0_alloc_t alloc) {
  *client = (a0_rpc_client_t)A0_EMPTY;
  client->_alloc = alloc;
  // Outstanding requests must be initialized before the response reader is opened to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_vec_init(
      &client->_outstanding_requests,
      sizeof(_a0_rpc_client_request_t)));

  a0_err_t err = a0_rpc_topic_open(topic, &client->_file);
  if (err) {
    a0_vec_close(&client->_outstanding_requests);
    return err;
  }

  err = a0_writer_init(&client->_request_writer, client->_file.arena);
  if (err) {
    a0_file_close(&client->_file);
    a0_vec_close(&client->_outstanding_requests);
    return err;
  }

  err = a0_writer_push(&client->_request_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&client->_request_writer);
    a0_file_close(&client->_file);
    a0_vec_close(&client->_outstanding_requests);
    return err;
  }

  err = a0_reader_init(
      &client->_response_reader,
      client->_file.arena,
      alloc,
      (a0_reader_options_t){A0_INIT_AWAIT_NEW, A0_ITER_NEXT},
      (a0_packet_callback_t){
          .user_data = client,
          .fn = a0_rpc_client_onpacket,
      });

  if (err) {
    a0_writer_close(&client->_request_writer);
    a0_file_close(&client->_file);
    a0_vec_close(&client->_outstanding_requests);
    return err;
  }

  a0_rpc_deadman_open(topic, &client->_deadman);

  pthread_create(&client->_deadman_thread, NULL, a0_rpc_client_deadman_thread, client);

  return A0_OK;
}

a0_err_t a0_rpc_client_close(a0_rpc_client_t* client) {
  a0_reader_close(&client->_response_reader);

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  client->_closing = true;
  a0_deadman_close(&client->_deadman);
  a0_cnd_broadcast(&client->_cnd, &client->_mtx);
  a0_mtx_unlock(&client->_mtx);
  pthread_join(client->_deadman_thread, NULL);
  if (client->_timeout_thread_created) {
    pthread_join(client->_timeout_thread, NULL);
  }

  a0_writer_close(&client->_request_writer);
  a0_file_close(&client->_file);

  // Dealloc outstanding requests.
  size_t size;
  a0_vec_size(&client->_outstanding_requests, &size);

  for (size_t i = 0; i < size; i++) {
    _a0_rpc_client_request_t* iter;
    a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);
    _a0_malloc_dealloc(NULL, iter->pkt_buf);
  }

  a0_vec_close(&client->_outstanding_requests);
  return A0_OK;
}

a0_err_t a0_rpc_client_send_opts(a0_rpc_client_t* client, a0_packet_t pkt, a0_packet_callback_t onreply, a0_rpc_client_send_options_t opts) {
  // Save the request info for future responses.
  _a0_rpc_client_request_t req = A0_EMPTY;
  a0_packet_deep_copy(pkt, _a0_malloc(), &req.pkt, &req.pkt_buf);
  if (opts.timeout) {
    req.timeout = *opts.timeout;
    req.has_timeout = true;
    req.ontimeout = opts.ontimeout;
  }
  req.onreply = onreply;
  req.onreconnect = opts.onreconnect;

  A0_UNUSED(a0_mtx_lock(&client->_mtx));

  a0_vec_push_back(&client->_outstanding_requests, &req);
  a0_rpc_client_dosend(client, req.pkt);

  if (req.has_timeout) {
    a0_cnd_broadcast(&client->_cnd, &client->_mtx);
  }

  a0_mtx_unlock(&client->_mtx);

  if (req.has_timeout && !client->_timeout_thread_created) {
    pthread_create(&client->_timeout_thread, NULL, a0_rpc_client_timeout_thread, client);
    client->_timeout_thread_created = true;
  }

  return A0_OK;
}

a0_err_t a0_rpc_client_send(a0_rpc_client_t* client, a0_packet_t pkt, a0_packet_callback_t onreply) {
  return a0_rpc_client_send_opts(client, pkt, onreply, (a0_rpc_client_send_options_t)A0_EMPTY);
}

a0_err_t a0_rpc_client_cancel(a0_rpc_client_t* client, const a0_uuid_t reqid) {
  _a0_outstanding_requests_pop(client, reqid, NULL);

  a0_packet_t pkt;
  a0_packet_init(&pkt);

  const size_t num_headers = 3;
  a0_packet_header_t headers[] = {
      {RPC_TYPE, RPC_TYPE_CANCEL},
      {REQUEST_ID, reqid},
      {A0_DEP, reqid},
  };

  pkt.headers_block = (a0_packet_headers_block_t){
      .headers = headers,
      .size = num_headers,
      .next_block = NULL,
  };
  pkt.payload = (a0_buf_t){(uint8_t*)reqid, sizeof(a0_uuid_t)};

  return a0_writer_write(&client->_request_writer, pkt);
}

a0_err_t a0_rpc_client_server_wait_up(a0_rpc_client_t* client, uint64_t* out_tkn) {
  return a0_deadman_wait_taken(&client->_deadman, out_tkn);
}

a0_err_t a0_rpc_client_server_timedwait_up(a0_rpc_client_t* client, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  return a0_deadman_timedwait_taken(&client->_deadman, timeout, out_tkn);
}

a0_err_t a0_rpc_client_server_wait_down(a0_rpc_client_t* client, uint64_t tkn) {
  return a0_deadman_wait_released(&client->_deadman, tkn);
}

a0_err_t a0_rpc_client_server_timedwait_down(a0_rpc_client_t* client, a0_time_mono_t* timeout, uint64_t tkn) {
  return a0_deadman_timedwait_released(&client->_deadman, timeout, tkn);
}

a0_err_t a0_rpc_client_server_state(a0_rpc_client_t* client, a0_deadman_state_t* state) {
  return a0_deadman_state(&client->_deadman, state);
}
