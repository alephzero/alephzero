#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/compare.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/map.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/reader.h>
#include <a0/uuid.h>
#include <a0/writer.h>
#include <a0/writer_middleware.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_util.h"
#include "protocol_util.h"

static const char PRPC_TYPE[] = "a0_prpc_type";
static const char PRPC_TYPE_CONNECT[] = "connect";
static const char PRPC_TYPE_PROGRESS[] = "progress";
static const char PRPC_TYPE_COMPLETE[] = "complete";
static const char PRPC_TYPE_CANCEL[] = "cancel";

static const char CONN_ID[] = "a0_conn_id";

A0_STATIC_INLINE
errno_t _a0_prpc_open_topic(a0_prpc_topic_t topic, a0_file_t* file) {
  const char* template = getenv("A0_PRPC_TOPIC_TEMPLATE");
  if (!template) {
    template = "alephzero/{topic}.prpc.a0";
  }
  return a0_open_topic(template, topic.name, topic.file_opts, file);
}

////////////
// Server //
////////////

A0_STATIC_INLINE
void a0_prpc_server_onpacket(void* data, a0_packet_t pkt) {
  a0_prpc_server_t* server = (a0_prpc_server_t*)data;

  const char* type;
  if (a0_find_header(pkt, PRPC_TYPE, &type)) {
    return;
  }
  if (!strcmp(type, PRPC_TYPE_CONNECT)) {
    server->_onconnect.fn(server->_onconnect.user_data, (a0_prpc_connection_t){server, pkt});
  } else if (!strcmp(type, PRPC_TYPE_CANCEL)) {
    if (server->_oncancel.fn) {
      a0_uuid_t uuid;
      memcpy(uuid, pkt.payload.ptr, A0_UUID_SIZE);
      server->_oncancel.fn(server->_oncancel.user_data, uuid);
    }
  }
}

errno_t a0_prpc_server_init(a0_prpc_server_t* server,
                            a0_prpc_topic_t topic,
                            a0_alloc_t alloc,
                            a0_prpc_connection_callback_t onconnect,
                            a0_packet_id_callback_t oncancel) {
  server->_onconnect = onconnect;
  server->_oncancel = oncancel;

  // Progress writer must be set up before the connection reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(_a0_prpc_open_topic(topic, &server->_file));

  errno_t err = a0_writer_init(&server->_progress_writer, server->_file.arena);
  if (err) {
    a0_file_close(&server->_file);
    return err;
  }

  err = a0_writer_push(
      &server->_progress_writer,
      a0_writer_middleware_add_standard_headers());
  if (err) {
    a0_writer_close(&server->_progress_writer);
    a0_file_close(&server->_file);
    return err;
  }

  err = a0_reader_init(
      &server->_connection_reader,
      server->_file.arena,
      alloc,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){
          .user_data = server,
          .fn = a0_prpc_server_onpacket,
      });
  if (err) {
    a0_writer_close(&server->_progress_writer);
    a0_file_close(&server->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_prpc_server_close(a0_prpc_server_t* server) {
  a0_reader_close(&server->_connection_reader);
  a0_writer_close(&server->_progress_writer);
  a0_file_close(&server->_file);
  return A0_OK;
}

errno_t a0_prpc_server_send(a0_prpc_connection_t conn, a0_packet_t resp, bool done) {
  const size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[] = {
      {PRPC_TYPE, done ? PRPC_TYPE_COMPLETE : PRPC_TYPE_PROGRESS},
      {CONN_ID, (char*)conn.pkt.id},
      {A0_PACKET_DEP_KEY, (char*)conn.pkt.id},
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
void _a0_prpc_client_onpacket(void* user_data, a0_packet_t pkt) {
  a0_prpc_client_t* client = (a0_prpc_client_t*)user_data;

  a0_uuid_t* conn_id;
  if (a0_find_header(pkt, CONN_ID, (const char**)&conn_id)) {
    return;
  }

  const char* type;
  if (a0_find_header(pkt, PRPC_TYPE, (const char**)&type)) {
    return;
  }

  bool is_complete = !strcmp(type, PRPC_TYPE_COMPLETE);

  errno_t err;
  a0_prpc_progress_callback_t cb;
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  if (is_complete) {
    err = a0_map_pop(&client->_outstanding_connections, conn_id, &cb);
  } else {
    a0_prpc_progress_callback_t* cb_ptr;
    err = a0_map_get(&client->_outstanding_connections, conn_id, (void**)&cb_ptr);
    if (!err) {
      cb = *cb_ptr;
    }
  }
  pthread_mutex_unlock(&client->_outstanding_connections_mu);

  if (!err) {
    cb.fn(cb.user_data, pkt, is_complete);
  }
}

errno_t a0_prpc_client_init(a0_prpc_client_t* client,
                            a0_prpc_topic_t topic,
                            a0_alloc_t alloc) {
  // Outstanding connections must be initialized before the response reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_map_init(
      &client->_outstanding_connections,
      sizeof(a0_uuid_t),
      sizeof(a0_prpc_progress_callback_t),
      A0_HASH_UUID,
      A0_COMPARE_UUID));
  pthread_mutex_init(&client->_outstanding_connections_mu, NULL);

  errno_t err = _a0_prpc_open_topic(topic, &client->_file);
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

  err = a0_writer_push(
      &client->_connection_writer,
      a0_writer_middleware_add_standard_headers());
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
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){
          .user_data = client,
          .fn = _a0_prpc_client_onpacket,
      });
  if (err) {
    a0_writer_close(&client->_connection_writer);
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_connections);
    pthread_mutex_destroy(&client->_outstanding_connections_mu);
    return err;
  }

  return A0_OK;
}

errno_t a0_prpc_client_close(a0_prpc_client_t* client) {
  a0_reader_close(&client->_progress_reader);
  a0_writer_close(&client->_connection_writer);
  a0_file_close(&client->_file);
  a0_map_close(&client->_outstanding_connections);
  pthread_mutex_destroy(&client->_outstanding_connections_mu);
  return A0_OK;
}

errno_t a0_prpc_client_connect(a0_prpc_client_t* client, a0_packet_t pkt, a0_prpc_progress_callback_t onprogress) {
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  errno_t err = a0_map_put(&client->_outstanding_connections, pkt.id, &onprogress);
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

errno_t a0_prpc_client_cancel(a0_prpc_client_t* client, const a0_uuid_t uuid) {
  pthread_mutex_lock(&client->_outstanding_connections_mu);
  a0_map_del(&client->_outstanding_connections, uuid);
  pthread_mutex_unlock(&client->_outstanding_connections_mu);

  a0_packet_t pkt;
  a0_packet_init(&pkt);

  const size_t num_headers = 3;
  a0_packet_header_t headers[] = {
      {PRPC_TYPE, PRPC_TYPE_CANCEL},
      {CONN_ID, uuid},
      {A0_PACKET_DEP_KEY, uuid},
  };

  pkt.headers_block = (a0_packet_headers_block_t){
      .headers = headers,
      .size = num_headers,
      .next_block = NULL,
  };
  pkt.payload = (a0_buf_t){
      .ptr = (uint8_t*)uuid,
      .size = A0_UUID_SIZE,
  };

  return a0_writer_write(&client->_connection_writer, pkt);
}
