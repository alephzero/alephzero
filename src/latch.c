#include <a0/err.h>
#include <a0/latch.h>
#include <a0/time.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "atomic.h"
#include "err_macro.h"
#include "ftx.h"

a0_err_t a0_latch_init(a0_latch_t* l, int32_t init_val) {
  a0_atomic_store(l, init_val);
  return A0_OK;
}

a0_err_t a0_latch_count_down(a0_latch_t* l, int32_t update) {
  int32_t new_val = a0_atomic_add_fetch(l, -update);
  if (new_val <= 0) {
    a0_ftx_broadcast((a0_ftx_t*)l);
  }
  return A0_OK;
}

a0_err_t a0_latch_try_wait(a0_latch_t* l, bool* out) {
  *out = a0_atomic_load(l) <= 0;
  return A0_OK;
}

a0_err_t a0_latch_wait(a0_latch_t* l) {
  int32_t val = a0_atomic_load(l);
  while (val > 0) {
    a0_err_t err = a0_ftx_wait((a0_ftx_t*)l, val, A0_TIMEOUT_NEVER);
    if (err && A0_SYSERR(err) != EAGAIN) {
      return err;
    }
    val = a0_atomic_load(l);
  }
  return A0_OK;
}

a0_err_t a0_latch_arrive_and_wait(a0_latch_t* l, int32_t update) {
  a0_latch_count_down(l, update);
  a0_latch_wait(l);
  return A0_OK;
}
