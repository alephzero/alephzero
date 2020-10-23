#ifndef A0_REF_CNT_H
#define A0_REF_CNT_H

#include <a0/errno.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

errno_t a0_ref_cnt_inc(void*);
errno_t a0_ref_cnt_dec(void*);
errno_t a0_ref_cnt_get(void*, size_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_REF_CNT_H
