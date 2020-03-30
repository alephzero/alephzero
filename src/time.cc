#include <a0/time.h>

#include <chrono>

#include "to_chars.hpp"

const char kMonoTime[] = "a0_mono_time";
const char kWallTime[] = "a0_wall_time";

errno_t a0_time_mono_now(uint64_t* out) {
  timespec mono_ts;
  clock_gettime(CLOCK_MONOTONIC, &mono_ts);
  *out = mono_ts.tv_sec * uint64_t(1e9) + mono_ts.tv_nsec;
  return A0_OK;
}

errno_t a0_time_wall_now(timespec* out) {
  clock_gettime(CLOCK_REALTIME, out);
  return A0_OK;
}

errno_t a0_time_mono_str(uint64_t mono_ts, char mono_str[20]) {
  // Mono time as unsigned integer with up to 20 chars: "18446744072709551615"
  a0::to_chars(mono_str, mono_str + 19, mono_ts);
  mono_str[19] = '\0';
  return A0_OK;
}

errno_t a0_time_wall_str(timespec wall_ts, char wall_str[36]) {
  // Wall time in RFC 3999 Nano: "2006-01-02T15:04:05.999999999Z07:00"
  std::tm wall_tm;
  gmtime_r(&wall_ts.tv_sec, &wall_tm);

  std::strftime(&wall_str[0], 20, "%Y-%m-%dT%H:%M:%S", &wall_tm);
  std::snprintf(&wall_str[19], 17, ".%09ldZ00:00", wall_ts.tv_nsec);
  wall_str[35] = '\0';

  return A0_OK;
}
