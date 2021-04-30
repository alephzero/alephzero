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

///////////////
// Publisher //
///////////////

typedef struct a0_publisher_s {
  a0_file_t _file;
  a0_writer_t _simple_writer;
  a0_writer_t _annotated_writer;
} a0_publisher_t;

errno_t a0_publisher_init(
    a0_publisher_t*,
    const char* topic,
    const a0_file_options_t* topic_opts);
errno_t a0_publisher_close(a0_publisher_t*);
errno_t a0_publisher_pub(a0_publisher_t*, a0_packet_t);

////////////////
// Subscriber //
////////////////

// Synchronous zero-copy version.

typedef struct a0_subscriber_sync_zc_s {
  a0_file_t _file;
  a0_reader_sync_zc_t _reader_sync_zc;
} a0_subscriber_sync_zc_t;

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t*,
                                   const char* topic,
                                   const a0_file_options_t* topic_opts,
                                   a0_reader_init_t,
                                   a0_reader_iter_t);

errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t*);

errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t*, bool*);
errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t*, a0_zero_copy_callback_t);

// Synchronous allocated version.

typedef struct a0_subscriber_sync_s {
  a0_file_t _file;
  a0_reader_sync_t _reader_sync;
} a0_subscriber_sync_t;

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t*,
                                const char* topic,
                                const a0_file_options_t* topic_opts,
                                a0_alloc_t,
                                a0_reader_init_t,
                                a0_reader_iter_t);

errno_t a0_subscriber_sync_close(a0_subscriber_sync_t*);

errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t*, bool*);
errno_t a0_subscriber_sync_next(a0_subscriber_sync_t*, a0_packet_t*);

// Threaded zero-copy version.

typedef struct a0_subscriber_zc_s {
  a0_file_t _file;
  a0_reader_zc_t _reader_zc;
} a0_subscriber_zc_t;

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t*,
                              const char* topic,
                              const a0_file_options_t* topic_opts,
                              a0_reader_init_t,
                              a0_reader_iter_t,
                              a0_zero_copy_callback_t);

errno_t a0_subscriber_zc_close(a0_subscriber_zc_t*);

// Threaded allocated version.

typedef struct a0_subscriber_impl_s a0_subscriber_impl_t;

typedef struct a0_subscriber_s {
  a0_file_t _file;
  a0_reader_t _reader;
} a0_subscriber_t;

errno_t a0_subscriber_init(a0_subscriber_t*,
                           const char* topic,
                           const a0_file_options_t* topic_opts,
                           a0_alloc_t,
                           a0_reader_init_t,
                           a0_reader_iter_t,
                           a0_packet_callback_t);

errno_t a0_subscriber_close(a0_subscriber_t*);

// One-off reader.

// Defaults to blocking mode.
// Pass O_NDELAY or O_NONBLOCK to flags to run non-blocking.
// If non-blocking and transport is empty, returns EAGAIN.

errno_t a0_subscriber_read_one(const char* topic,
                               const a0_file_options_t* topic_opts,
                               a0_alloc_t,
                               a0_reader_init_t,
                               int flags,
                               a0_packet_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_PUBSUB_H
