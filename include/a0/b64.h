#ifndef A0_B64_H
#define A0_B64_H

#include <a0/common.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

errno_t b64_encode(a0_buf_t original, a0_alloc_t, a0_buf_t* out_encoded);

errno_t b64_decode(a0_buf_t encoded, a0_alloc_t, a0_buf_t* out_decoded);

#ifdef __cplusplus
}
#endif

#endif  // A0_B64_H
