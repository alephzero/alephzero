#include <a0/time.h>

#include <chrono>

#include "to_chars.hpp"

const char kMonoTime[] = "a0_mono_time";
const char kWallTime[] = "a0_wall_time";

errno_t a0_time_mono_str(char mono_str[20]) {
  // Mono time as unsigned integer with up to 20 chars: "18446744072709551615"
  timespec mono_ts;
  clock_gettime(CLOCK_MONOTONIC, &mono_ts);

  a0::to_chars(mono_str, mono_str + 19, mono_ts.tv_sec * uint64_t(1e9) + mono_ts.tv_nsec);
  mono_str[19] = '\0';

  return A0_OK;
}

errno_t a0_time_wall_str(char wall_str[36]) {
  // Wall time in RFC 3999 Nano: "2006-01-02T15:04:05.999999999Z07:00"
  timespec wall_ts;
  clock_gettime(CLOCK_REALTIME, &wall_ts);

  std::tm wall_tm;
  gmtime_r(&wall_ts.tv_sec, &wall_tm);

  std::strftime(&wall_str[0], 20, "%Y-%m-%dT%H:%M:%S", &wall_tm);
  std::snprintf(&wall_str[19], 17, ".%09ldZ00:00", wall_ts.tv_nsec);
  wall_str[35] = '\0';

  return A0_OK;
}
