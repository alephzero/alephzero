#ifndef A0_PUBSUB_H
#define A0_PUBSUB_H

#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/transport.h>
#include <a0/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_pubsub_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_pubsub_topic_t;

///////////////
// Publisher //
///////////////

typedef struct a0_publisher_s {
  a0_file_t _file;
  a0_writer_t _writer;
} a0_publisher_t;

a0_err_t a0_publisher_init(a0_publisher_t*, a0_pubsub_topic_t);
a0_err_t a0_publisher_close(a0_publisher_t*);
a0_err_t a0_publisher_pub(a0_publisher_t*, a0_packet_t);

////////////////
// Subscriber //
////////////////

// Synchronous allocated version.

typedef struct a0_subscriber_sync_s {
  a0_file_t _file;
  a0_reader_sync_t _reader_sync;
} a0_subscriber_sync_t;

a0_err_t a0_subscriber_sync_init(a0_subscriber_sync_t*,
                                 a0_pubsub_topic_t,
                                 a0_alloc_t,
                                 a0_reader_init_t,
                                 a0_reader_iter_t);

a0_err_t a0_subscriber_sync_close(a0_subscriber_sync_t*);

a0_err_t a0_subscriber_sync_can_read(a0_subscriber_sync_t*, bool*);
a0_err_t a0_subscriber_sync_read(a0_subscriber_sync_t*, a0_packet_t*);
a0_err_t a0_subscriber_sync_read_blocking(a0_subscriber_sync_t*, a0_packet_t*);
a0_err_t a0_subscriber_sync_read_blocking_timeout(a0_subscriber_sync_t*, a0_time_mono_t, a0_packet_t*);

// Threaded allocated version.

typedef struct a0_subscriber_s {
  a0_file_t _file;
  a0_reader_t _reader;
} a0_subscriber_t;

a0_err_t a0_subscriber_init(a0_subscriber_t*,
                            a0_pubsub_topic_t,
                            a0_alloc_t,
                            a0_reader_init_t,
                            a0_reader_iter_t,
                            a0_packet_callback_t);

a0_err_t a0_subscriber_close(a0_subscriber_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_PUBSUB_H
