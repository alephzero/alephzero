// Necessary for strptime.
#define _GNU_SOURCE

#include <a0/err.h>
#include <a0/time.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "clock.h"
#include "err_util.h"
#include "strconv.h"

const char A0_TIME_MONO[] = "a0_time_mono";

errno_t a0_time_mono_now(a0_time_mono_t* out) {
  return a0_clock_now(CLOCK_BOOTTIME, &out->ts);
}

errno_t a0_time_mono_str(a0_time_mono_t time_mono, char mono_str[20]) {
  // Mono time as unsigned integer with up to 20 chars: "18446744072709551615"
  uint64_t ns = time_mono.ts.tv_sec * NS_PER_SEC + time_mono.ts.tv_nsec;
  mono_str[19] = '\0';
  return a0_u64_to_str(ns, mono_str, mono_str + 19, NULL);
}

errno_t a0_time_mono_parse(const char mono_str[20], a0_time_mono_t* out) {
  uint64_t ns;
  A0_RETURN_ERR_ON_ERR(a0_str_to_u64(mono_str, mono_str + 19, &ns));
  out->ts.tv_sec = ns / NS_PER_SEC;
  out->ts.tv_nsec = ns % NS_PER_SEC;
  return A0_OK;
}

errno_t a0_time_mono_add(a0_time_mono_t time_mono, int64_t add_nsec, a0_time_mono_t* out) {
  return a0_clock_add(time_mono.ts, add_nsec, &out->ts);
}

const char A0_TIME_WALL[] = "a0_time_wall";

errno_t a0_time_wall_now(a0_time_wall_t* out) {
  A0_RETURN_ERR_ON_MINUS_ONE(clock_gettime(CLOCK_REALTIME, &out->ts));
  return A0_OK;
}

errno_t a0_time_wall_str(a0_time_wall_t wall_time, char wall_str[36]) {
  // Wall time in RFC 3999 Nano: "2006-01-02T15:04:05.999999999-07:00"
  struct tm wall_tm;
  gmtime_r(&wall_time.ts.tv_sec, &wall_tm);

  strftime(&wall_str[0], 20, "%Y-%m-%dT%H:%M:%S", &wall_tm);
  snprintf(&wall_str[19], 17, ".%09ld-00:00", wall_time.ts.tv_nsec);
  wall_str[35] = '\0';

  return A0_OK;
}

errno_t a0_time_wall_parse(const char wall_str[36], a0_time_wall_t* out) {
  struct tm wall_tm;
  if (!strptime(wall_str, "%Y-%m-%dT%H:%M:%S", &wall_tm)) {
    return EINVAL;
  }

  out->ts.tv_sec = timegm(&wall_tm);
  return a0_str_to_u64(wall_str + 20, wall_str + 29, (uint64_t*)&out->ts.tv_nsec);
}
