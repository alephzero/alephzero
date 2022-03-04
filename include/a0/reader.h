/**
 * \file reader.h
 * \rst
 *
 * Example
 * ---------------
 *
 * .. code-block:: cpp
 *
 *   // callback triggers on each written packet.
 *   a0::Reader reader(a0::File("path"), callback);
 *
 * .. code-block:: cpp
 *
 *   a0::ReaderSync reader(a0::File("path"), a0.INIT_OLDEST);
 *   while (reader.can_read()) {
 *     a0::Packet packet = reader.read();
 *     ...
 *   }
 *
 *
 * An optional **INIT** can be added to specify where the reader starts.
 *
 * * **INIT_AWAIT_NEW** (default): Start with messages written after the creation of the reader.
 * * **INIT_MOST_RECENT**: Start with the most recently written message. Useful for state and configuration. But be careful, this can be quite old!
 * * **INIT_OLDEST**: Start with the oldest message still in available in the transport.
 *
 * An optional **ITER** can be added to specify how to continue reading messages. After each callback:
 *
 * * **ITER_NEXT** (default): grab the sequentially next message. When you don't want to miss a thing.
 * * **ITER_NEWEST**: grab the newest available unread message. When you want to keep up with the firehose.
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

typedef struct a0_reader_options_s {
  a0_reader_init_t init;
  a0_reader_iter_t iter;
} a0_reader_options_t;

extern const a0_reader_options_t A0_READER_OPTIONS_DEFAULT;

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
  a0_reader_options_t _opts;
  bool _first_read_done;
} a0_reader_sync_zc_t;

/// ...
a0_err_t a0_reader_sync_zc_init(a0_reader_sync_zc_t*,
                                a0_arena_t,
                                a0_reader_options_t);

/// ...
a0_err_t a0_reader_sync_zc_close(a0_reader_sync_zc_t*);

/// ...
a0_err_t a0_reader_sync_zc_can_read(a0_reader_sync_zc_t*, bool*);

/// ...
a0_err_t a0_reader_sync_zc_read(a0_reader_sync_zc_t*, a0_zero_copy_callback_t);

/// ...
a0_err_t a0_reader_sync_zc_read_blocking(a0_reader_sync_zc_t*, a0_zero_copy_callback_t);

/// ...
a0_err_t a0_reader_sync_zc_read_blocking_timeout(a0_reader_sync_zc_t*, a0_time_mono_t*, a0_zero_copy_callback_t);

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
                             a0_reader_options_t);

/// ...
a0_err_t a0_reader_sync_close(a0_reader_sync_t*);

/// ...
a0_err_t a0_reader_sync_can_read(a0_reader_sync_t*, bool*);

/// ...
a0_err_t a0_reader_sync_read(a0_reader_sync_t*, a0_packet_t*);

/// ...
a0_err_t a0_reader_sync_read_blocking(a0_reader_sync_t*, a0_packet_t*);

/// ...
a0_err_t a0_reader_sync_read_blocking_timeout(a0_reader_sync_t*, a0_time_mono_t*, a0_packet_t*);

/** @}*/

/** \addtogroup READER_ZC
 *  @{
 */

typedef struct a0_reader_zc_s {
  a0_transport_t _transport;
  bool _started_empty;

  a0_reader_options_t _opts;

  a0_zero_copy_callback_t _onpacket;

  pthread_t _thread;
  uint32_t _thread_id;
  a0_event_t _thread_start_event;
} a0_reader_zc_t;

/// ...
a0_err_t a0_reader_zc_init(a0_reader_zc_t*,
                           a0_arena_t,
                           a0_reader_options_t,
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
                        a0_reader_options_t,
                        a0_packet_callback_t);

/// ...
a0_err_t a0_reader_close(a0_reader_t*);

/** @}*/

/// ...
a0_err_t a0_read_random_access(a0_arena_t, size_t off, a0_zero_copy_callback_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_READER_H
