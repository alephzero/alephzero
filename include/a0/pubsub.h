#ifndef A0_PUBSUB_H
#define A0_PUBSUB_H

#include <a0/alloc.h>
#include <a0/common.h>
#include <a0/packet.h>
#include <a0/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_zero_copy_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_locked_transport_t, a0_packet_t);
} a0_zero_copy_callback_t;

///////////////
// Publisher //
///////////////

typedef struct a0_publisher_raw_impl_s a0_publisher_raw_impl_t;

typedef struct a0_publisher_raw_s {
  a0_publisher_raw_impl_t* _impl;
} a0_publisher_raw_t;

errno_t a0_publisher_raw_init(a0_publisher_raw_t*, a0_arena_t);
errno_t a0_publisher_raw_close(a0_publisher_raw_t*);
errno_t a0_pub_raw(a0_publisher_raw_t*, a0_packet_t);

typedef struct a0_publisher_impl_s a0_publisher_impl_t;

typedef struct a0_publisher_s {
  a0_publisher_impl_t* _impl;
} a0_publisher_t;

errno_t a0_publisher_init(a0_publisher_t*, a0_arena_t);
errno_t a0_publisher_close(a0_publisher_t*);
errno_t a0_pub(a0_publisher_t*, a0_packet_t);

////////////////
// Subscriber //
////////////////

typedef enum a0_subscriber_init_s {
  A0_INIT_OLDEST,
  A0_INIT_MOST_RECENT,
  A0_INIT_AWAIT_NEW,
} a0_subscriber_init_t;

typedef enum a0_subscriber_iter_s {
  A0_ITER_NEXT,
  A0_ITER_NEWEST,
} a0_subscriber_iter_t;

// Synchronous zero-copy version.

typedef struct a0_subscriber_sync_zc_impl_s a0_subscriber_sync_zc_impl_t;

typedef struct a0_subscriber_sync_zc_s {
  a0_subscriber_sync_zc_impl_t* _impl;
} a0_subscriber_sync_zc_t;

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t*,
                                   a0_arena_t,
                                   a0_subscriber_init_t,
                                   a0_subscriber_iter_t);

errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t*);

errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t*, bool*);
errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t*, a0_zero_copy_callback_t);

// Synchronous allocated version.

typedef struct a0_subscriber_sync_impl_s a0_subscriber_sync_impl_t;

typedef struct a0_subscriber_sync_s {
  a0_subscriber_sync_impl_t* _impl;
} a0_subscriber_sync_t;

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t*,
                                a0_arena_t,
                                a0_alloc_t,
                                a0_subscriber_init_t,
                                a0_subscriber_iter_t);

errno_t a0_subscriber_sync_close(a0_subscriber_sync_t*);

errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t*, bool*);
errno_t a0_subscriber_sync_next(a0_subscriber_sync_t*, a0_packet_t*);

// Threaded zero-copy version.

typedef struct a0_subscriber_zc_impl_s a0_subscriber_zc_impl_t;

typedef struct a0_subscriber_zc_s {
  a0_subscriber_zc_impl_t* _impl;
} a0_subscriber_zc_t;

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t*,
                              a0_arena_t,
                              a0_subscriber_init_t,
                              a0_subscriber_iter_t,
                              a0_zero_copy_callback_t);

errno_t a0_subscriber_zc_close(a0_subscriber_zc_t*);
errno_t a0_subscriber_zc_async_close(a0_subscriber_zc_t*, a0_callback_t);

// Threaded allocated version.

typedef struct a0_subscriber_impl_s a0_subscriber_impl_t;

typedef struct a0_subscriber_s {
  a0_subscriber_impl_t* _impl;
} a0_subscriber_t;

errno_t a0_subscriber_init(a0_subscriber_t*,
                           a0_arena_t,
                           a0_alloc_t,
                           a0_subscriber_init_t,
                           a0_subscriber_iter_t,
                           a0_packet_callback_t);

errno_t a0_subscriber_close(a0_subscriber_t*);
errno_t a0_subscriber_async_close(a0_subscriber_t*, a0_callback_t);

// One-off reader.

// Defaults to blocking mode.
// Pass O_NDELAY or O_NONBLOCK to flags to run non-blocking.
// If non-blocking and transport is empty, returns EAGAIN.

errno_t a0_subscriber_read_one(a0_arena_t,
                               a0_alloc_t,
                               a0_subscriber_init_t,
                               int flags,
                               a0_packet_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_PUBSUB_H
