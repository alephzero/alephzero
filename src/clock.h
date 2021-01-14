#ifndef A0_SRC_CLOCK_H
#define A0_SRC_CLOCK_H

#include "macros.h"

typedef struct timespec timespec_t;

A0_STATIC_INLINE
timespec_t a0_clock_now(clockid_t clk) {
  timespec_t now;
  clock_gettime(clk, &now);
  return now;
}

A0_STATIC_INLINE
timespec_t a0_clock_add(timespec_t ts0, timespec_t ts1) {
  const int64_t ns_per_sec = 1e9;

  int64_t add_nsec = 1e9 * ts1.tv_sec + ts1.tv_nsec;

  ts0.tv_sec += add_nsec / ns_per_sec;
  ts0.tv_nsec += add_nsec % ns_per_sec;
  if (ts0.tv_nsec >= ns_per_sec) {
    ts0.tv_sec++;
    ts0.tv_nsec -= ns_per_sec;
  } else if (ts0.tv_nsec < 0) {
    ts0.tv_sec--;
    ts0.tv_nsec += ns_per_sec;
  }

  return ts0;
}

A0_STATIC_INLINE
timespec_t a0_clock_dur(int64_t dur) {
  timespec_t zero = A0_EMPTY;
  timespec_t delta = {.tv_sec = 0, .tv_nsec = dur};
  return a0_clock_add(zero, delta);
}

A0_STATIC_INLINE
timespec_t a0_clock_convert(
    timespec_t orig_ts,
    clockid_t orig_clk,
    clockid_t target_clk) {
  timespec_t orig_now = a0_clock_now(orig_clk);

  timespec_t dur = {
      .tv_sec = orig_ts.tv_sec - orig_now.tv_sec,
      .tv_nsec = orig_ts.tv_nsec - orig_now.tv_nsec,
  };
  return a0_clock_add(a0_clock_now(target_clk), dur);
}

#endif  // A0_SRC_CLOCK_H
