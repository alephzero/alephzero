#ifndef A0_TIME_H
#define A0_TIME_H

#include <a0/common.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char kMonoTime[];
extern const char kWallTime[];

errno_t a0_time_mono_str(char mono_str[20]);
errno_t a0_time_wall_str(char wall_str[36]);

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
