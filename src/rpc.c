#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/compare.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/map.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/rpc.h>
#include <a0/uuid.h>
#include <a0/writer.h>

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_util.h"
#include "protocol_util.h"

static const char RPC_TYPE[] = "a0_rpc_type";
static const char RPC_TYPE_REQUEST[] = "request";
static const char RPC_TYPE_RESPONSE[] = "response";
static const char RPC_TYPE_CANCEL[] = "cancel";

static const char REQUEST_ID[] = "a0_req_id";

A0_STATIC_INLINE
errno_t _a0_rpc_open_topic(a0_rpc_topic_t topic, a0_file_t* file) {
  const char* template = getenv("A0_RPC_TOPIC_TEMPLATE");
  if (!template) {
    template = "alephzero/{topic}.rpc.a0";
  }
  return a0_open_topic(template, topic.name, topic.file_opts, file);
}

////////////
// Server //
////////////

A0_STATIC_INLINE
void a0_rpc_server_onpacket(void* data, a0_packet_t pkt) {
  a0_rpc_server_t* server = (a0_rpc_server_t*)data;

  const char* type;
  if (a0_find_header(pkt, RPC_TYPE, &type)) {
    return;
  }
  if (!strcmp(type, RPC_TYPE_REQUEST)) {
    server->_onrequest.fn(server->_onrequest.user_data, (a0_rpc_request_t){server, pkt});
  } else if (!strcmp(type, RPC_TYPE_CANCEL)) {
    if (server->_oncancel.fn) {
      a0_uuid_t uuid;
      memcpy(uuid, pkt.payload.ptr, A0_UUID_SIZE);
      server->_oncancel.fn(server->_oncancel.user_data, uuid);
    }
  }
}

errno_t a0_rpc_server_init(a0_rpc_server_t* server,
                           a0_rpc_topic_t topic,
                           a0_alloc_t alloc,
                           a0_rpc_request_callback_t onrequest,
                           a0_packet_id_callback_t oncancel) {
  server->_onrequest = onrequest;
  server->_oncancel = oncancel;

  // Response writer must be set up before the request reader to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(_a0_rpc_open_topic(topic, &server->_file));

  errno_t err = a0_writer_init(&server->_response_writer, server->_file.arena);
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

  err = a0_reader_init(
      &server->_request_reader,
      server->_file.arena,
      alloc,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){
          .user_data = server,
          .fn = a0_rpc_server_onpacket,
      });
  if (err) {
    a0_writer_close(&server->_response_writer);
    a0_file_close(&server->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_rpc_server_close(a0_rpc_server_t* server) {
  a0_reader_close(&server->_request_reader);
  a0_writer_close(&server->_response_writer);
  a0_file_close(&server->_file);
  return A0_OK;
}

errno_t a0_rpc_server_reply(a0_rpc_request_t req, a0_packet_t resp) {
  const size_t num_extra_headers = 3;
  a0_packet_header_t extra_headers[] = {
      {RPC_TYPE, RPC_TYPE_RESPONSE},
      {REQUEST_ID, (char*)req.pkt.id},
      {A0_PACKET_DEP_KEY, (char*)req.pkt.id},
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
void _a0_rpc_client_onpacket(void* user_data, a0_packet_t pkt) {
  a0_rpc_client_t* client = (a0_rpc_client_t*)user_data;

  a0_uuid_t* req_id;
  if (a0_find_header(pkt, REQUEST_ID, (const char**)&req_id)) {
    return;
  }

  a0_packet_callback_t cb;
  pthread_mutex_lock(&client->_outstanding_requests_mu);
  errno_t err = a0_map_pop(&client->_outstanding_requests, req_id, &cb);
  pthread_mutex_unlock(&client->_outstanding_requests_mu);

  if (!err) {
    a0_packet_callback_call(cb, pkt);
  }
}

errno_t a0_rpc_client_init(a0_rpc_client_t* client,
                           a0_rpc_topic_t topic,
                           a0_alloc_t alloc) {
  // Outstanding requests must be initialized before the response reader is opened to avoid a race condition.

  A0_RETURN_ERR_ON_ERR(a0_map_init(
      &client->_outstanding_requests,
      sizeof(a0_uuid_t),
      sizeof(a0_packet_callback_t),
      A0_HASH_UUID,
      A0_COMPARE_UUID));
  pthread_mutex_init(&client->_outstanding_requests_mu, NULL);

  errno_t err = _a0_rpc_open_topic(topic, &client->_file);
  if (err) {
    a0_map_close(&client->_outstanding_requests);
    pthread_mutex_destroy(&client->_outstanding_requests_mu);
    return err;
  }

  err = a0_writer_init(&client->_request_writer, client->_file.arena);
  if (err) {
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_requests);
    pthread_mutex_destroy(&client->_outstanding_requests_mu);
    return err;
  }

  err = a0_writer_push(&client->_request_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&client->_request_writer);
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_requests);
    pthread_mutex_destroy(&client->_outstanding_requests_mu);
    return err;
  }

  err = a0_reader_init(
      &client->_response_reader,
      client->_file.arena,
      alloc,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){
          .user_data = client,
          .fn = _a0_rpc_client_onpacket,
      });
  if (err) {
    a0_writer_close(&client->_request_writer);
    a0_file_close(&client->_file);
    a0_map_close(&client->_outstanding_requests);
    pthread_mutex_destroy(&client->_outstanding_requests_mu);
    return err;
  }

  return A0_OK;
}

errno_t a0_rpc_client_close(a0_rpc_client_t* client) {
  a0_reader_close(&client->_response_reader);
  a0_writer_close(&client->_request_writer);
  a0_file_close(&client->_file);
  a0_map_close(&client->_outstanding_requests);
  pthread_mutex_destroy(&client->_outstanding_requests_mu);
  return A0_OK;
}

errno_t a0_rpc_client_send(a0_rpc_client_t* client, a0_packet_t pkt, a0_packet_callback_t onresponse) {
  pthread_mutex_lock(&client->_outstanding_requests_mu);
  errno_t err = a0_map_put(&client->_outstanding_requests, pkt.id, &onresponse);
  pthread_mutex_unlock(&client->_outstanding_requests_mu);
  A0_RETURN_ERR_ON_ERR(err);

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

  return a0_writer_write(&client->_request_writer, full_pkt);
}

errno_t a0_rpc_client_cancel(a0_rpc_client_t* client, const a0_uuid_t uuid) {
  pthread_mutex_lock(&client->_outstanding_requests_mu);
  a0_map_del(&client->_outstanding_requests, uuid);
  pthread_mutex_unlock(&client->_outstanding_requests_mu);

  a0_packet_t pkt;
  a0_packet_init(&pkt);

  const size_t num_headers = 3;
  a0_packet_header_t headers[] = {
      {RPC_TYPE, RPC_TYPE_CANCEL},
      {REQUEST_ID, uuid},
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

  return a0_writer_write(&client->_request_writer, pkt);
}
