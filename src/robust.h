#ifndef A0_SRC_ROBUST_H
#define A0_SRC_ROBUST_H

#include <a0/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

void a0_robust_op_start(a0_mtx_t*);
void a0_robust_op_end(a0_mtx_t*);
void a0_robust_op_add(a0_mtx_t*);
void a0_robust_op_del(a0_mtx_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_ROBUST_H
