#ifndef A0_TIME_H
#define A0_TIME_H

#include <a0/common.h>

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup TIME_MONO
 *  @{
 *
 * \rst
 * | Mono time is an uint64_t representing nanoseconds from some unknown start time.
 * | This time cannot decrease and duration between ticks is constant.
 * | This time is not related to wall clock time.
 * | This time is most suitable for measuring durations.
 * |
 * | As a packet header value, it is represented as a 20 char number:
 * | **18446744072709551615**
 * \endrst
 */

/// Header key for monotonic timestamps.
extern const char A0_TIME_MONO[];

/// Get the current monotonic timestamps.
errno_t a0_time_mono_now(uint64_t*);

/// Stringify a given monotonic timestamps.
errno_t a0_time_mono_str(uint64_t, char mono_str[20]);
/** @}*/

/** \addtogroup TIME_WALL
 *  @{
 *
 * \rst
 * | Wall time is an timespec representing human-readable wall clock time.
 * | This time can decrease and duration between ticks is not constant.
 * | This time is most related to wall clock time.
 * | This time is not suitable for measuring durations.
 * |
 * | As a packet header value, it is represented as a 36 char RFC 3999 Nano (ISO 8601):
 * | **2006-01-02T15:04:05.999999999-07:00**
 * \endrst
 */

/// Header key for wall timestamps.
extern const char A0_TIME_WALL[];

/// Get the current wall timestamps.
errno_t a0_time_wall_now(timespec*);

/// Stringify a given wall timestamps.
errno_t a0_time_wall_str(timespec, char wall_str[36]);
/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
