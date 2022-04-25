#ifndef A0_LATCH_H
#define A0_LATCH_H

#include <a0/err.h>
#include <a0/mtx.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_latch_s {
  int32_t _val;
  a0_mtx_t _mtx;
  a0_cnd_t _cnd;
} a0_latch_t;

a0_err_t a0_latch_init(a0_latch_t*, int32_t init_val);

a0_err_t a0_latch_count_down(a0_latch_t*, int32_t update);

a0_err_t a0_latch_try_wait(a0_latch_t*, bool*);

a0_err_t a0_latch_wait(a0_latch_t*);

a0_err_t a0_latch_arrive_and_wait(a0_latch_t*, int32_t update);

#ifdef __cplusplus
}
#endif

#endif  // A0_LATCH_H
