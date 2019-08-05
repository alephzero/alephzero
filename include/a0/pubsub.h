#ifndef A0_PUBSUB_H
#define A0_PUBSUB_H

#include <a0/packet.h>
#include <a0/stream.h>

typedef struct a0_topic_s {
  a0_buf_t name;
  // If set, ignores mapping.
  a0_buf_t container;
} a0_topic_t;

///////////////
// Publisher //
///////////////

typedef struct a0_publisher_impl_s a0_publisher_impl_t;

typedef struct a0_publisher_s {
  a0_publisher_impl_t* _impl;
} a0_publisher_t;

errno_t a0_publisher_init(a0_publisher_t*, a0_topic_t);
errno_t a0_publisher_close(a0_publisher_t*);
errno_t a0_pub(a0_publisher_t*, a0_packet_t);

////////////////
// Subscriber //
////////////////

typedef enum a0_subscriber_read_start_s {
  A0_READ_START_EARLIEST,
  A0_READ_START_LATEST,
  A0_READ_START_NEW,
} a0_subscriber_read_start_t;

typedef enum a0_subscriber_read_next_s {
  A0_READ_NEXT_SEQUENTIAL,
  A0_READ_NEXT_RECENT,
} a0_subscriber_read_next_t;

// Zero-copy multi-threaded version.

typedef struct a0_subscriber_zero_copy_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_locked_stream_t, a0_packet_t);
} a0_subscriber_zero_copy_callback_t;

typedef struct a0_subscriber_zero_copy_impl_s a0_subscriber_zero_copy_impl_t;

typedef struct a0_subscriber_zero_copy_s {
  a0_subscriber_zero_copy_impl_t* _impl;
} a0_subscriber_zero_copy_t;

errno_t a0_subscriber_zero_copy_open(
    a0_subscriber_zero_copy_t*,
    a0_topic_t,
    a0_subscriber_read_start_t,
    a0_subscriber_read_next_t,
    a0_subscriber_zero_copy_callback_t);

errno_t a0_subscriber_zero_copy_close(a0_subscriber_zero_copy_t*, a0_callback_t);

// Multi-threaded version.

typedef struct a0_subscriber_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t);
} a0_subscriber_callback_t;

typedef struct a0_subscriber_impl_s a0_subscriber_impl_t;

typedef struct a0_subscriber_s {
  a0_subscriber_impl_t* _impl;
} a0_subscriber_t;

errno_t a0_subscriber_open(
    a0_subscriber_t*,
    a0_topic_t,
    a0_subscriber_read_start_t,
    a0_subscriber_read_next_t,
    a0_alloc_t,
    a0_subscriber_callback_t);

errno_t a0_subscriber_close(a0_subscriber_t*, a0_callback_t);

// FD version.

typedef struct a0_subscriber_fd_impl_s a0_subscriber_fd_impl_t;

typedef struct a0_subscriber_fd_s {
  a0_subscriber_fd_impl_t* _impl;
} a0_subscriber_fd_t;

errno_t a0_subscriber_fd_open(
    a0_subscriber_fd_t*,
    a0_topic_t,
    a0_subscriber_read_start_t,
    a0_subscriber_read_next_t,
    int* fd_out);

errno_t a0_subscriber_fd_read(a0_subscriber_fd_t*, a0_alloc_t, a0_packet_t*);

errno_t a0_subscriber_fd_close(a0_subscriber_fd_t*);

#endif  // A0_PUBSUB_H
