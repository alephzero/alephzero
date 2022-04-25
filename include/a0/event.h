#ifndef A0_EVENT_H
#define A0_EVENT_H

#include <a0/err.h>
#include <a0/mtx.h>
#include <a0/time.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_event_s {
  bool _val;
  a0_mtx_t _mtx;
  a0_cnd_t _cnd;
} a0_event_t;

a0_err_t a0_event_is_set(a0_event_t*, bool* out);

a0_err_t a0_event_set(a0_event_t*);

a0_err_t a0_event_wait(a0_event_t*);

a0_err_t a0_event_timedwait(a0_event_t*, a0_time_mono_t* timeout);

#ifdef __cplusplus
}
#endif

#endif  // A0_EVENT_H
