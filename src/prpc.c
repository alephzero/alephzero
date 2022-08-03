#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/cmp.h>
#include <a0/deadman.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/map.h>
#include <a0/middleware.h>
#include <a0/mtx.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/reader.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/unused.h>
#include <a0/uuid.h>
#include <a0/writer.h>

#include <alloca.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

static const char PRPC_TYPE[] = "a0_prpc_type";
static const char PRPC_TYPE_CONNECT[] = "connect";
static const char PRPC_TYPE_PROGRESS[] = "progress";
static const char PRPC_TYPE_COMPLETE[] = "complete";
static const char PRPC_TYPE_CANCEL[] = "cancel";

static const char CONN_ID[] = "a0_conn_id";

A0_STATIC_INLINE
a0_err_t a0_prpc_topic_open(a0_prpc_topic_t topic, a0_file_t* file) {
  return a0_topic_open(a0_env_topic_tmpl_prpc(), topic.name, topic.file_opts, file);
}

A0_STATIC_INLINE
a0_err_t a0_prpc_deadman_open(a0_prpc_topic_t topic, a0_deadman_t* deadman) {
  char* deadman_name = alloca(strlen(topic.name) + strlen(".prpc") + 1);
  strcpy(deadman_name, topic.name);
  strcpy(deadman_name + strlen(topic.name), ".prpc");

  a0_deadman_topic_t deadman_topic = {.name = deadman_name};
  a0_deadman_init(deadman, deadman_topic);

  return A0_OK;
}

////////////
// Server //
////////////

A0_STATIC_INLINE
void a0_prpc_server_onpacket(void* data, a0_packet_t pkt) {
  a0_prpc_server_t* server = (a0_prpc_server_t*)data;

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
  if (a0_packet_header_iterator_next_match(&hdr_iter, PRPC_TYPE, &type_hdr)) {
    return;
  }

  if (!strcmp(type_hdr.val, PRPC_TYPE_CONNECT)) {
    server->_onconnect.fn(server->_onconnect.user_data, (a0_prpc_connection_t){server, pkt});
  } else if (!strcmp(type_hdr.val, PRPC_TYPE_CANCEL)) {
    if (server->_oncancel.fn) {
      a0_uuid_t uuid;
      memcpy(uuid, pkt.payload.data, sizeof(a0_uuid_t));
      server->_oncancel.fn(server->_oncancel.user_data, uuid);
    }
  }
}

a0_err_t a0_prpc_server_init(a0_prpc_server_t* server,
                             a0_prpc_topic_t topic,
                             a0_alloc_t alloc,
                             a0_prpc_server_options_t opts) {
  server->_onconnect = opts.onconnect;
  server->_oncancel = opts.oncancel;

  // Progress writer must be set up before the connection reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_prpc_topic_open(topic, &server->_file));

  a0_err_t err = a0_writer_init(&server->_progress_writer, server->_file.arena);
  if (err) {
    a0_file_close(&server->_file);
    return err;
  }

  err = a0_writer_push(&server->_progress_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&server->_progress_writer);
    a0_file_close(&server->_file);
    return err;
  }

  // Prevent the reader from processing until the deadman is acquired.
  A0_UNUSED(a0_mtx_lock(&server->_init_lock));

  err = a0_reader_init(
      &server->_connection_reader,
      server->_file.arena,
      alloc,
      (a0_reader_options_t){A0_INIT_AWAIT_NEW, A0_ITER_NEXT},
      (a0_packet_callback_t){
          .user_data = server,
          .fn = a0_prpc_server_onpacket,
      });
  if (err) {
    a0_writer_close(&server->_progress_writer);
    a0_file_close(&server->_file);
    return err;
  }

  a0_prpc_deadman_open(topic, &server->_deadman);
  err = a0_deadman_timedtake(&server->_deadman, opts.exclusive_ownership_timeout);
  if (!a0_mtx_lock_successful(err)) {
    a0_mtx_unlock(&server->_init_lock);
    a0_reader_close(&server->_connection_reader);
    a0_writer_close(&server->_progress_writer);
    a0_file_close(&server->_file);
    a0_deadman_close(&server->_deadman);
    return err;
  }
  server->_init_complete = true;
  a0_mtx_unlock(&server->_init_lock);

  return A0_OK;
}

a0_err_t a0_prpc_server_close(a0_prpc_server_t* server) {
  a0_reader_close(&server->_connection_reader);
  a0_writer_close(&server->_progress_writer);
  a0_file_close(&server->_file);
  a0_deadman_close(&server->_deadman);
  return A0_OK;
}

a0_err_t a0_prpc_server_send(a0_prpc_connection_t conn, a0_packet_t resp, bool done) {
  const size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[] = {
      {PRPC_TYPE, done ? PRPC_TYPE_COMPLETE : PRPC_TYPE_PROGRESS},
      {CONN_ID, (char*)conn.pkt.id},
      {A0_DEP, (char*)conn.pkt.id},
  };

  a0_packet_t full_resp = resp;
  full_resp.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&resp.headers_block,
  };

  return a0_writer_write(&conn.server->_progress_writer, full_resp);
}

////////////
// Client //
////////////

A0_STATIC_INLINE
void a0_prpc_client_onpacket(void* user_data, a0_packet_t pkt) {
  a0_prpc_client_t* client = (a0_prpc_client_t*)user_data;

  a0_packet_header_iterator_t hdr_iter;

  a0_packet_header_t conn_id_hdr;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  if (a0_packet_header_iterator_next_match(&hdr_iter, CONN_ID, &conn_id_hdr)) {
    return;
  }

  a0_packet_header_t type_hdr;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  if (a0_packet_header_iterator_next_match(&hdr_iter, PRPC_TYPE, &type_hdr)) {
    return;
  }

  bool is_complete = !strcmp(type_hdr.val, PRPC_TYPE_COMPLETE);

  a0_err_t err;
  a0_prpc_progress_callback_t cb;
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  if (is_complete) {
    err = a0_map_pop(&client->_outstanding_connections, conn_id_hdr.val, &cb);
  } else {
    a0_prpc_progress_callback_t* cb_ptr;
    err = a0_map_get(&client->_outstanding_connections, conn_id_hdr.val, (void**)&cb_ptr);
    if (!err) {
      cb = *cb_ptr;
    }
  }
  pthread_mutex_unlock(&client->_outstanding_connections_mu);

  if (!err) {
    cb.fn(cb.user_data, pkt, is_complete);
  }
}

a0_err_t a0_prpc_client_init(a0_prpc_client_t* client,
                             a0_prpc_topic_t topic,
                             a0_alloc_t alloc) {
  // Outstanding connections must be initialized before the response reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_map_init(
      &client->_outstanding_connections,
      sizeof(a0_uuid_t),
      sizeof(a0_prpc_progress_callback_t),
      A0_HASH_UUID,
      A0_CMP_UUID));
  pthread_mutex_init(&client->_outstanding_connections_mu, NULL);

  a0_err_t err = a0_prpc_topic_open(topic, &client->_file);
  if (err) {
    a0_map_close(&client->_outstanding_connections);
    pthread_mutex_destroy(&client->_outstanding_connections_mu);
    return err;
  }

  err = a0_writer_init(&client->_connection_writer, client->_file.arena);
  if (err) {
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_connections);
    pthread_mutex_destroy(&client->_outstanding_connections_mu);
    return err;
  }

  err = a0_writer_push(&client->_connection_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&client->_connection_writer);
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_connections);
    pthread_mutex_destroy(&client->_outstanding_connections_mu);
    return err;
  }

  err = a0_reader_init(
      &client->_progress_reader,
      client->_file.arena,
      alloc,
      (a0_reader_options_t){A0_INIT_AWAIT_NEW, A0_ITER_NEXT},
      (a0_packet_callback_t){
          .user_data = client,
          .fn = a0_prpc_client_onpacket,
      });
  if (err) {
    a0_writer_close(&client->_connection_writer);
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_connections);
    pthread_mutex_destroy(&client->_outstanding_connections_mu);
    return err;
  }

  a0_prpc_deadman_open(topic, &client->_deadman);

  return A0_OK;
}

a0_err_t a0_prpc_client_close(a0_prpc_client_t* client) {
  a0_reader_close(&client->_progress_reader);
  a0_writer_close(&client->_connection_writer);
  a0_file_close(&client->_file);
  a0_deadman_close(&client->_deadman);
  a0_map_close(&client->_outstanding_connections);
  pthread_mutex_destroy(&client->_outstanding_connections_mu);
  return A0_OK;
}

a0_err_t a0_prpc_client_connect(a0_prpc_client_t* client, a0_packet_t pkt, a0_prpc_progress_callback_t onprogress) {
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  a0_err_t err = a0_map_put(&client->_outstanding_connections, pkt.id, &onprogress);
  pthread_mutex_unlock(&client->_outstanding_connections_mu);
  A0_RETURN_ERR_ON_ERR(err);

  const size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[] = {
      {PRPC_TYPE, PRPC_TYPE_CONNECT},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_writer_write(&client->_connection_writer, full_pkt);
}

a0_err_t a0_prpc_client_cancel(a0_prpc_client_t* client, const a0_uuid_t uuid) {
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  a0_map_del(&client->_outstanding_connections, uuid);
  pthread_mutex_unlock(&client->_outstanding_connections_mu);

  a0_packet_t pkt;
  a0_packet_init(&pkt);

  const size_t num_headers = 3;
  a0_packet_header_t headers[] = {
      {PRPC_TYPE, PRPC_TYPE_CANCEL},
      {CONN_ID, uuid},
      {A0_DEP, uuid},
  };

  pkt.headers_block = (a0_packet_headers_block_t){
      .headers = headers,
      .size = num_headers,
      .next_block = NULL,
  };
  pkt.payload = (a0_buf_t){(uint8_t*)uuid, sizeof(a0_uuid_t)};

  return a0_writer_write(&client->_connection_writer, pkt);
}

A0_STATIC_INLINE
a0_err_t _ignore_owner_died(a0_err_t err) {
  if (a0_mtx_previous_owner_died(err)) {
    return A0_OK;
  }
  return err;
}

a0_err_t a0_prpc_client_server_deadman(a0_prpc_client_t* client, a0_deadman_t** out) {
  *out = &client->_deadman;
  return A0_OK;
}

a0_err_t a0_prpc_client_server_wait_up(a0_prpc_client_t* client, uint64_t* out_tkn) {
  return _ignore_owner_died(a0_deadman_wait_taken(&client->_deadman, out_tkn));
}

a0_err_t a0_prpc_client_server_timedwait_up(a0_prpc_client_t* client, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  return _ignore_owner_died(a0_deadman_timedwait_taken(&client->_deadman, timeout, out_tkn));
}

a0_err_t a0_prpc_client_server_wait_down(a0_prpc_client_t* client, uint64_t tkn) {
  return _ignore_owner_died(a0_deadman_wait_released(&client->_deadman, tkn));
}

a0_err_t a0_prpc_client_server_timedwait_down(a0_prpc_client_t* client, a0_time_mono_t* timeout, uint64_t tkn) {
  return _ignore_owner_died(a0_deadman_timedwait_released(&client->_deadman, timeout, tkn));
}

a0_err_t a0_prpc_client_server_state(a0_prpc_client_t* client, a0_deadman_state_t* state) {
  return _ignore_owner_died(a0_deadman_state(&client->_deadman, state));
}
