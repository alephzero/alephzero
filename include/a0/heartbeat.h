/**
 * \file heartbeat.h
 * \rst
 *
 * Heartbeat
 * ---------
 *
 * A heartbeat is a signal, published at a fixed frequency, which can act as
 * a proof of life to listeners. On its own, it does not convey any health
 * status information more granular than the last published timestamp.
 *
 * A Heartbeat is parameterized by its publish frequency. For example, a
 * frequency of 10 is interpreted as 10Hz and will publish every 100ms.
 *
 * If a publish frequency is not provided, it defaults to 10Hz.
 *
 * Heartbeat Listener
 * ------------------
 *
 * A heartbeat listener subscribes to some heartbeat and will execute
 * callbacks based on changes in state.
 *
 * The first callback, **ondetected**, will be triggered as soon as the first
 * heartbeat is detected. This may be immediately if there a heartbeat was
 * recently published on the given arena.
 *
 * The second callback, **onmissed**, will only trigger after **ondetected**,
 * when a heartbeat takes longer than a given minimum frequency.
 *
 * Both callbacks may trigger at most once.
 *
 * If a minimum frequency is not provided, it defaults to 5Hz.
 *
 * .. note::
 *
 *    Heartbeat Listener is parameterized by a **minimum** acceptable
 *    frequency. This should be less than the heartbeat frequency, otherwise
 *    false positives are likely to occur.
 *
 * \endrst
 */

#ifndef A0_HEARTBEAT_H
#define A0_HEARTBEAT_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup HEARTBEAT
 *  @{
 */

/// Options for a heartbeat publisher.
typedef struct a0_heartbeat_options_s {
  /// Desired frequency of published heartbeats.
  double freq;
} a0_heartbeat_options_t;

/// Heartbeat options used in none are explicitly provided.
/// Defaults to a 10Hz frequency.
extern const a0_heartbeat_options_t A0_HEARTBEAT_OPTIONS_DEFAULT;

typedef struct a0_heartbeat_impl_s a0_heartbeat_impl_t;

/// A heartbeat publisher.
typedef struct a0_heartbeat_s {
  a0_heartbeat_impl_t* _impl;
} a0_heartbeat_t;

/// Initializes a heartbeat publisher.
errno_t a0_heartbeat_init(a0_heartbeat_t*, a0_arena_t, const a0_heartbeat_options_t*);
/// Stops and shuts down a heartbeat publisher.
errno_t a0_heartbeat_close(a0_heartbeat_t*);

/** @}*/

/** \addtogroup HEARTBEAT_LISTENER
 *  @{
 */

/// Options for a heartbeat listener.
typedef struct a0_heartbeat_listener_options_s {
  /// Minimum acceptable frequency of heartbeats.
  double min_freq;
} a0_heartbeat_listener_options_t;

/// Heartbeat options used in none are explicitly provided.
/// Defaults to 5Hz acceptable frequency.
extern const a0_heartbeat_listener_options_t A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT;

typedef struct a0_heartbeat_listener_impl_s a0_heartbeat_listener_impl_t;

/// A heartbeat listener.
typedef struct a0_heartbeat_listener_s {
  a0_heartbeat_listener_impl_t* _impl;
} a0_heartbeat_listener_t;

/// Initializes a heartbeat listener.
/// TODO: If heartbeat packets are fixed-size, remove alloc and use stack space.
errno_t a0_heartbeat_listener_init(a0_heartbeat_listener_t*,
                                   a0_arena_t arena,
                                   a0_alloc_t alloc,
                                   const a0_heartbeat_listener_options_t*,
                                   a0_callback_t ondetected,
                                   a0_callback_t onmissed);

/// Stops and shuts down a heartbeat listener.
/// The callbacks will no not trigger after this returns.
/// This will block if a callback is currently running.
/// This cannot be called from inside a callback.
errno_t a0_heartbeat_listener_close(a0_heartbeat_listener_t*);

/// Stops and shuts down a heartbeat listener.
/// This is intended to be called from within a heartbeat listener callback.
/// This returns immediately, and the given callback will execute when the
/// close operation is complete.
errno_t a0_heartbeat_listener_async_close(a0_heartbeat_listener_t*, a0_callback_t);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_HEARTBEAT_H
