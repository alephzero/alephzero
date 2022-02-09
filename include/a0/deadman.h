#ifndef A0_DEADMAN_H
#define A0_DEADMAN_H

#include <a0/mtx.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_deadman_s {
  a0_mtx_t _guard;
  a0_cnd_t _acquire_cnd;
  a0_mtx_t _owner_mtx;
  uint64_t _tkn;
  bool _acquired;
} a0_deadman_t;

a0_err_t a0_deadman_acquire(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_tryacquire(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedacquire(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);

a0_err_t a0_deadman_release(a0_deadman_t*);

a0_err_t a0_deadman_wait_acquired(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedwait_acquired(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_isacquired(a0_deadman_t*, bool*, uint64_t* out_tkn);

a0_err_t a0_deadman_wait_released(a0_deadman_t*, uint64_t tkn);
a0_err_t a0_deadman_wait_released_any(a0_deadman_t*);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t*, uint64_t tkn);
a0_err_t a0_deadman_timedwait_released_any(a0_deadman_t*, a0_time_mono_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_H
