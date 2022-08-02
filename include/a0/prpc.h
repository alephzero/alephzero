#ifndef A0_PRPC_H
#define A0_PRPC_H

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

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_prpc_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_prpc_topic_t;

////////////
// Server //
////////////

typedef struct a0_prpc_server_s a0_prpc_server_t;

typedef struct a0_prpc_connection_s {
  a0_prpc_server_t* server;
  a0_packet_t pkt;
} a0_prpc_connection_t;

typedef struct a0_prpc_connection_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_prpc_connection_t);
} a0_prpc_connection_callback_t;

typedef struct a0_prpc_server_options_s {
  a0_prpc_connection_callback_t onconnect;
  a0_packet_id_callback_t oncancel;

  a0_time_mono_t* exclusive_ownership_timeout;
} a0_prpc_server_options_t;

struct a0_prpc_server_s {
  a0_file_t _file;
  a0_reader_t _connection_reader;
  a0_writer_t _progress_writer;

  a0_deadman_t _deadman;

  bool _init_complete;
  a0_mtx_t _init_lock;

  a0_prpc_connection_callback_t _onconnect;
  a0_packet_id_callback_t _oncancel;
};

a0_err_t a0_prpc_server_init(a0_prpc_server_t*,
                             a0_prpc_topic_t,
                             a0_alloc_t,
                             a0_prpc_server_options_t);
a0_err_t a0_prpc_server_close(a0_prpc_server_t*);
// Note: do NOT respond with the request packet. The ids MUST be unique!
a0_err_t a0_prpc_server_send(a0_prpc_connection_t, a0_packet_t, bool done);

////////////
// Client //
////////////

typedef struct a0_prpc_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t, bool done);
} a0_prpc_progress_callback_t;

typedef struct a0_prpc_client_s {
  a0_file_t _file;
  a0_writer_t _connection_writer;
  a0_reader_t _progress_reader;

  a0_deadman_t _deadman;

  a0_map_t _outstanding_connections;
  pthread_mutex_t _outstanding_connections_mu;
} a0_prpc_client_t;

a0_err_t a0_prpc_client_init(a0_prpc_client_t*, a0_prpc_topic_t, a0_alloc_t);
a0_err_t a0_prpc_client_close(a0_prpc_client_t*);
a0_err_t a0_prpc_client_connect(a0_prpc_client_t*, a0_packet_t, a0_prpc_progress_callback_t);
// Note: use the same packet that was provided to a0_prpc_connect.
a0_err_t a0_prpc_client_cancel(a0_prpc_client_t*, const a0_uuid_t);

a0_err_t a0_prpc_client_server_wait_up(a0_prpc_client_t*, uint64_t* out_tkn);
a0_err_t a0_prpc_client_server_timedwait_up(a0_prpc_client_t*, a0_time_mono_t*, uint64_t* out_tkn);
a0_err_t a0_prpc_client_server_wait_down(a0_prpc_client_t*, uint64_t tkn);
a0_err_t a0_prpc_client_server_timedwait_down(a0_prpc_client_t*, a0_time_mono_t*, uint64_t tkn);
a0_err_t a0_prpc_client_server_state(a0_prpc_client_t*, a0_deadman_state_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_PRPC_H
