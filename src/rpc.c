#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/cmp.h>
#include <a0/empty.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/map.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/rpc.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/uuid.h>
#include <a0/writer.h>

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

  char* deadman_name = alloca(strlen(topic.name) + strlen(".rpc") + 1);
  memcpy(deadman_name, topic.name, strlen(topic.name));
  memcpy(deadman_name + strlen(topic.name), ".rpc\0", 5);

  a0_deadman_topic_t deadman_topic = {.name = deadman_name};
  a0_deadman_init(&server->_deadman, deadman_topic);
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
  a0_time_mono_t timeout;
  bool has_timeout;
  a0_packet_callback_t onreply;
  a0_callback_t ontimeout;
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
    client->_server_connected = true;
    client->_server_tkn = server_tkn;

    size_t size;
    a0_vec_size(&client->_outstanding_requests, &size);

    for (size_t i = 0; i < size; i++) {
      _a0_rpc_client_request_t* iter;
      a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);
      a0_rpc_client_dosend(client, iter->pkt);
    }

    a0_mtx_unlock(&client->_mtx);
    a0_deadman_wait_released(&client->_deadman, server_tkn);
    A0_UNUSED(a0_mtx_lock(&client->_mtx));
  }
  a0_mtx_unlock(&client->_mtx);

  return NULL;
}

A0_STATIC_INLINE
a0_vec_t a0_rpc_client_timeout_thread_pop_expired(a0_rpc_client_t* client) {
  a0_vec_t expired;
  a0_vec_init(&expired, sizeof(_a0_rpc_client_request_t));

  size_t size;
  a0_vec_size(&client->_outstanding_requests, &size);

  if (!size) {
    return expired;
  }

  a0_time_mono_t now;
  a0_time_mono_now(&now);

  size_t i = 0;
  while (i < size) {
    _a0_rpc_client_request_t* iter;
    a0_vec_at(&client->_outstanding_requests, i, (void**)&iter);

    if (iter->has_timeout) {
      bool is_old;
      a0_time_mono_less(iter->timeout, now, &is_old);
      if (is_old) {
        a0_vec_push_back(&expired, iter);
        a0_vec_swap_back_pop(&client->_outstanding_requests, i, NULL);
        size--;
      }
    } else {
      i++;
    }
  }

  return expired;
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

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  while (!client->_closing) {
    a0_vec_t expired = a0_rpc_client_timeout_thread_pop_expired(client);

    a0_mtx_unlock(&client->_mtx);

    size_t expired_size;
    a0_vec_size(&expired, &expired_size);

    for (size_t i = 0; i < expired_size; i++) {
      _a0_rpc_client_request_t* iter;
      a0_vec_at(&expired, i, (void**)&iter);
      a0_callback_call(iter->ontimeout);
      _a0_malloc_dealloc(NULL, iter->pkt_buf);
    }
    a0_vec_close(&expired);

    A0_UNUSED(a0_mtx_lock(&client->_mtx));
    if (client->_closing) {
      break;
    }

    a0_time_mono_t* timeout = a0_rpc_client_timeout_thread_min_timeout(client);
    a0_cnd_timedwait(&client->_cnd, &client->_mtx, timeout);
  }
  a0_mtx_unlock(&client->_mtx);
  return NULL;
}

a0_err_t a0_rpc_client_init(a0_rpc_client_t* client, a0_rpc_topic_t topic) {
  *client = (a0_rpc_client_t)A0_EMPTY;
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
      _a0_malloc(),
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

  char* deadman_name = alloca(strlen(topic.name) + strlen(".rpc") + 1);
  memcpy(deadman_name, topic.name, strlen(topic.name));
  memcpy(deadman_name + strlen(topic.name), ".rpc\0", 5);

  a0_deadman_topic_t deadman_topic = {.name = deadman_name};
  a0_deadman_init(&client->_deadman, deadman_topic);

  pthread_create(&client->_deadman_thread, NULL, a0_rpc_client_deadman_thread, client);
  pthread_create(&client->_timeout_thread, NULL, a0_rpc_client_timeout_thread, client);

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
  pthread_join(client->_timeout_thread, NULL);

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

a0_err_t a0_rpc_client_send_timeout(a0_rpc_client_t* client, a0_packet_t pkt, a0_time_mono_t* timeout, a0_packet_callback_t onreply, a0_callback_t ontimeout) {
  // Save the request info for future responses.
  _a0_rpc_client_request_t req = A0_EMPTY;
  a0_packet_deep_copy(pkt, _a0_malloc(), &req.pkt, &req.pkt_buf);
  if (timeout) {
    req.timeout = *timeout;
    req.has_timeout = true;
  }
  req.onreply = onreply;
  req.ontimeout = ontimeout;

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  a0_vec_push_back(&client->_outstanding_requests, &req);

  if (client->_server_connected) {
    a0_rpc_client_dosend(client, pkt);
  }
  if (timeout) {
    a0_cnd_broadcast(&client->_cnd, &client->_mtx);
  }

  a0_mtx_unlock(&client->_mtx);
  return A0_OK;
}

a0_err_t a0_rpc_client_send(a0_rpc_client_t* client, a0_packet_t pkt, a0_packet_callback_t onreply) {
  return a0_rpc_client_send_timeout(client, pkt, A0_TIMEOUT_NEVER, onreply, (a0_callback_t)A0_EMPTY);
}

typedef struct a0_rpc_client_send_blocking_data_s {
  a0_rpc_client_t* client;
  a0_alloc_t alloc;
  a0_packet_t* out;
  a0_err_t err;
  bool done;
} a0_rpc_client_send_blocking_data_t;

A0_STATIC_INLINE
void a0_rpc_client_send_blocking_onreply(void* user_data, a0_packet_t pkt) {
  a0_rpc_client_send_blocking_data_t* data = (a0_rpc_client_send_blocking_data_t*)user_data;
  A0_UNUSED(a0_mtx_lock(&data->client->_mtx));
  if (!data->done) {
    data->done = true;
    a0_buf_t unused;
    a0_packet_deep_copy(pkt, data->alloc, data->out, &unused);
    a0_cnd_broadcast(&data->client->_cnd, &data->client->_mtx);
  }
  a0_mtx_unlock(&data->client->_mtx);
}

A0_STATIC_INLINE
a0_err_t a0_rpc_client_send_blocking_ontimeout(void* user_data) {
  a0_rpc_client_send_blocking_data_t* data = (a0_rpc_client_send_blocking_data_t*)user_data;
  A0_UNUSED(a0_mtx_lock(&data->client->_mtx));
  if (!data->done) {
    data->done = true;
    data->err = A0_ERR_TIMEDOUT;
    a0_cnd_broadcast(&data->client->_cnd, &data->client->_mtx);
  }
  a0_mtx_unlock(&data->client->_mtx);
  return A0_OK;
}

a0_err_t a0_rpc_client_send_blocking_timeout(a0_rpc_client_t* client, a0_packet_t pkt, a0_time_mono_t* timeout, a0_alloc_t alloc, a0_packet_t* out) {
  a0_rpc_client_send_blocking_data_t data = A0_EMPTY;
  data.client = client;
  data.alloc = alloc;
  data.out = out;

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn = a0_rpc_client_send_blocking_onreply,
  };

  a0_callback_t ontimeout = {
      .user_data = &data,
      .fn = a0_rpc_client_send_blocking_ontimeout,
  };

  a0_rpc_client_send_timeout(client, pkt, timeout, onreply, ontimeout);

  A0_UNUSED(a0_mtx_lock(&client->_mtx));
  while (!data.done) {
    a0_err_t err = a0_cnd_timedwait(&client->_cnd, &client->_mtx, timeout);
    if (A0_SYSERR(err) == ETIMEDOUT) {
      _a0_outstanding_requests_pop_locked(client, pkt.id, NULL);
      a0_mtx_unlock(&client->_mtx);
      return err;
    }
    if (data.done) {
      return data.err;
    }
  }
  a0_mtx_unlock(&client->_mtx);
  return data.err;
}

a0_err_t a0_rpc_client_send_blocking(a0_rpc_client_t* client, a0_packet_t pkt, a0_alloc_t alloc, a0_packet_t* out) {
  return a0_rpc_client_send_blocking_timeout(client, pkt, A0_TIMEOUT_NEVER, alloc, out);
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
