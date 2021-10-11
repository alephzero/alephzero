#ifndef A0_SRC_REF_CNT_H
#define A0_SRC_REF_CNT_H

#include <a0/err.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

a0_err_t a0_ref_cnt_inc(void*, size_t*);
a0_err_t a0_ref_cnt_dec(void*, size_t*);
a0_err_t a0_ref_cnt_get(void*, size_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_REF_CNT_H
