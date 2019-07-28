#ifndef A0_B64_H
#define A0_B64_H

#include <a0/common.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

errno_t b64_encode(const uint8_t* src,
                   size_t size,
                   uint8_t** out,
                   size_t* out_size);

errno_t b64_decode(const uint8_t* src,
                   size_t size,
                   uint8_t** out,
                   size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif  // A0_B64_H
