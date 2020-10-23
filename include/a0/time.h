/**
 * \file time.h
 * \rst
 *
 * Mono Time
 * ---------
 *
 * | Mono time is a number of nanoseconds from some unknown start time.
 * | This time cannot decrease and duration between ticks is constant.
 * | This time is not related to wall clock time.
 * | This time is most suitable for measuring durations.
 * |
 * | As a string, it is represented as a 20 char number:
 * | **18446744072709551615**
 *
 * Wall Time
 * ---------
 *
 * | Wall time is an time object representing human-readable wall clock time.
 * | This time can decrease and duration between ticks is not constant.
 * | This time is most related to wall clock time.
 * | This time is not suitable for measuring durations.
 * |
 * | As a string, it is represented as a 36 char RFC 3999 Nano / ISO 8601:
 * | **2006-01-02T15:04:05.999999999-07:00**
 *
 * \endrst
 */

#ifndef A0_TIME_H
#define A0_TIME_H

#include <a0/common.h>
#include <a0/errno.h>

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup TIME_MONO
 *  @{
 */

/// Header key for mono timestamps.
extern const char A0_TIME_MONO[];

/// Get the current mono timestamps.
errno_t a0_time_mono_now(uint64_t*);

/// Stringify a given mono timestamps.
errno_t a0_time_mono_str(uint64_t, char mono_str[20]);

/// Parse a stringified mono timestamps.
errno_t a0_time_mono_parse(const char mono_str[20], uint64_t*);
/** @}*/

/** \addtogroup TIME_WALL
 *  @{
 */

/// Header key for wall timestamps.
extern const char A0_TIME_WALL[];

/// Get the current wall timestamps.
errno_t a0_time_wall_now(struct timespec*);

/// Stringify a given wall timestamps.
errno_t a0_time_wall_str(struct timespec, char wall_str[36]);

/// Parse a stringified wall timestamps.
errno_t a0_time_wall_parse(const char wall_str[36], struct timespec*);
/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
