/**
 * \file reader.h
 * \rst
 *
 * Reader
 * ------
 *
 * \endrst
 */

#ifndef A0_READER_H
#define A0_READER_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/packet.h>
#include <a0/transport.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup READER_INIT
 *  @{
 */

typedef enum a0_reader_init_s {
  A0_INIT_OLDEST,
  A0_INIT_MOST_RECENT,
  A0_INIT_AWAIT_NEW,
} a0_reader_init_t;

/** @}*/

/** \addtogroup READER_ITER
 *  @{
 */

typedef enum a0_reader_iter_s {
  A0_ITER_NEXT,
  A0_ITER_NEWEST,
} a0_reader_iter_t;

/** @}*/

/// ...
typedef struct a0_zero_copy_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_transport_locked_t, a0_flat_packet_t);
} a0_zero_copy_callback_t;

/** \addtogroup READER_SYNC_ZC
 *  @{
 */

typedef struct a0_reader_sync_zc_s {
  a0_transport_t _transport;
  a0_reader_init_t _init;
  a0_reader_iter_t _iter;
  bool _first_read_done;
} a0_reader_sync_zc_t;

/// ...
a0_err_t a0_reader_sync_zc_init(a0_reader_sync_zc_t*,
                                a0_arena_t,
                                a0_reader_init_t,
                                a0_reader_iter_t);

/// ...
a0_err_t a0_reader_sync_zc_close(a0_reader_sync_zc_t*);

/// ...
a0_err_t a0_reader_sync_zc_has_next(a0_reader_sync_zc_t*, bool*);

/// ...
a0_err_t a0_reader_sync_zc_next(a0_reader_sync_zc_t*, a0_zero_copy_callback_t);

/** @}*/

/** \addtogroup READER_SYNC
 *  @{
 */

typedef struct a0_reader_sync_s {
  a0_reader_sync_zc_t _reader_sync_zc;
  a0_alloc_t _alloc;
} a0_reader_sync_t;

/// ...
a0_err_t a0_reader_sync_init(a0_reader_sync_t*,
                             a0_arena_t,
                             a0_alloc_t,
                             a0_reader_init_t,
                             a0_reader_iter_t);

/// ...
a0_err_t a0_reader_sync_close(a0_reader_sync_t*);

/// ...
a0_err_t a0_reader_sync_has_next(a0_reader_sync_t*, bool*);

/// ...
a0_err_t a0_reader_sync_next(a0_reader_sync_t*, a0_packet_t*);

/** @}*/

/** \addtogroup READER_ZC
 *  @{
 */

typedef struct a0_reader_zc_s {
  a0_transport_t _transport;
  bool _started_empty;

  a0_reader_init_t _init;
  a0_reader_iter_t _iter;

  a0_zero_copy_callback_t _onpacket;

  pthread_t _thread;
  uint32_t _thread_id;
  a0_event_t _thread_start_event;
} a0_reader_zc_t;

/// ...
a0_err_t a0_reader_zc_init(a0_reader_zc_t*,
                           a0_arena_t,
                           a0_reader_init_t,
                           a0_reader_iter_t,
                           a0_zero_copy_callback_t);

/// May not be called from within a callback.
a0_err_t a0_reader_zc_close(a0_reader_zc_t*);

/** @}*/

/** \addtogroup READER
 *  @{
 */

typedef struct a0_reader_s {
  a0_reader_zc_t _reader_zc;
  a0_alloc_t _alloc;
  a0_packet_callback_t _onpacket;
} a0_reader_t;

/// ...
a0_err_t a0_reader_init(a0_reader_t*,
                        a0_arena_t,
                        a0_alloc_t,
                        a0_reader_init_t,
                        a0_reader_iter_t,
                        a0_packet_callback_t);

/// ...
a0_err_t a0_reader_close(a0_reader_t*);

/** @}*/

/** \addtogroup READ_ONE
 *  @{
 */

// One-off reader.
// Defaults to blocking mode.
// Pass O_NDELAY or O_NONBLOCK to flags to run non-blocking.
// If non-blocking and transport is empty, returns EAGAIN.

a0_err_t a0_reader_read_one(a0_arena_t,
                            a0_alloc_t,
                            a0_reader_init_t,
                            int flags,
                            a0_packet_t*);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_READER_H
