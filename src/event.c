#include <a0/event.h>
#include <a0/time.h>

#include <errno.h>
#include <stdbool.h>
#include <time.h>

#include "atomic.h"
#include "clock.h"
#include "ftx.h"

a0_err_t a0_event_is_set(a0_event_t* evt, bool* out) {
  *out = a0_atomic_load(evt);
  return A0_OK;
}

a0_err_t a0_event_set(a0_event_t* evt) {
  a0_atomic_store(evt, true);
  a0_ftx_broadcast((a0_ftx_t*)evt);
  return A0_OK;
}

a0_err_t a0_event_wait(a0_event_t* evt) {
  return a0_event_timedwait(evt, A0_TIMEOUT_NEVER);
}

a0_err_t a0_event_timedwait(a0_event_t* evt, a0_time_mono_t* timeout) {
  bool val = a0_atomic_load(evt);
  while (!val) {
    a0_err_t err = a0_ftx_wait((a0_ftx_t*)evt, val, timeout);
    if (err && A0_SYSERR(err) != EAGAIN) {
      return err;
    }
    val = a0_atomic_load(evt);
  }
  return A0_OK;
}
