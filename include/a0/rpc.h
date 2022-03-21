#ifndef A0_RPC_H
#define A0_RPC_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/deadman.h>
#include <a0/file.h>
#include <a0/map.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_rpc_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_rpc_topic_t;

////////////
// Server //
////////////

typedef struct a0_rpc_server_s a0_rpc_server_t;

typedef struct a0_rpc_request_s {
  a0_rpc_server_t* server;
  a0_packet_t pkt;
} a0_rpc_request_t;

typedef struct a0_rpc_request_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_rpc_request_t);
} a0_rpc_request_callback_t;

typedef struct a0_rpc_server_options_s {
  a0_rpc_request_callback_t onrequest;
  a0_packet_id_callback_t oncancel;

  a0_time_mono_t* exclusive_ownership_timeout;
} a0_rpc_server_options_t;

struct a0_rpc_server_s {
  a0_file_t _file;
  a0_reader_t _request_reader;
  a0_writer_t _response_writer;

  a0_deadman_t _deadman;

  a0_rpc_request_callback_t _onrequest;
  a0_packet_id_callback_t _oncancel;
};

a0_err_t a0_rpc_server_init(a0_rpc_server_t*,
                            a0_rpc_topic_t,
                            a0_alloc_t,
                            a0_rpc_server_options_t);
a0_err_t a0_rpc_server_close(a0_rpc_server_t*);

// Note: do NOT respond with the request packet. The ids MUST be unique!
a0_err_t a0_rpc_server_reply(a0_rpc_request_t, a0_packet_t response);

////////////
// Client //
////////////

typedef struct a0_rpc_client_s {
  a0_file_t _file;
  a0_writer_t _request_writer;
  a0_reader_t _response_reader;

  a0_map_t _outstanding_requests;
  pthread_mutex_t _outstanding_requests_mu;
} a0_rpc_client_t;

a0_err_t a0_rpc_client_init(a0_rpc_client_t*, a0_rpc_topic_t, a0_alloc_t);
a0_err_t a0_rpc_client_close(a0_rpc_client_t*);

typedef struct a0_rpc_client_send_options_s {
  a0_packet_callback_t onreply;

  a0_time_mono_t* timeout;
  a0_callback_t ontimeout;
} a0_rpc_client_send_options_t;

a0_err_t a0_rpc_client_send(a0_rpc_client_t*, a0_packet_t, a0_packet_callback_t);
a0_err_t a0_rpc_client_send_blocking(a0_rpc_client_t*, a0_packet_t, a0_alloc_t, a0_packet_t* out);
a0_err_t a0_rpc_client_send_blocking_timeout(a0_rpc_client_t*, a0_packet_t, a0_time_mono_t*, a0_alloc_t, a0_packet_t* out);

// Note: use the id from the packet used in a0_rpc_client_send.
a0_err_t a0_rpc_client_cancel(a0_rpc_client_t*, const a0_uuid_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_RPC_H
