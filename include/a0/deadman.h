#ifndef A0_DEADMAN_H
#define A0_DEADMAN_H

#include <a0/rwmtx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_deadman_s {
  a0_rwmtx_t _rwmtx;
//   a0_rwcnd_t _rwcnd;

  a0_mtx_t _mtx;
  a0_cnd_t _cnd;
} a0_deadman_t;

a0_err_t a0_deadman_acquire(a0_deadman_t*);
a0_err_t a0_deadman_tryacquire(a0_deadman_t*);
a0_err_t a0_deadman_timedacquire(a0_deadman_t*, a0_time_mono_t);

a0_err_t a0_deadman_release(a0_deadman_t*);

a0_err_t a0_deadman_isactive(a0_deadman_t*, bool*);

a0_err_t a0_deadman_wait_active(a0_deadman_t*);
a0_err_t a0_deadman_wait_released(a0_deadman_t*);
a0_err_t a0_deadman_timedwait_active(a0_deadman_t*, a0_time_mono_t);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_H
