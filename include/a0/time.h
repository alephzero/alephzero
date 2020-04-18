#ifndef A0_TIME_H
#define A0_TIME_H

#include <a0/common.h>

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char A0_TIME_MONO[];
extern const char A0_TIME_WALL[];

errno_t a0_time_mono_now(uint64_t*);
errno_t a0_time_wall_now(timespec*);

errno_t a0_time_mono_str(uint64_t, char mono_str[20]);
errno_t a0_time_wall_str(timespec, char wall_str[36]);

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
