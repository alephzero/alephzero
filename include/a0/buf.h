#ifndef A0_BUF_H
#define A0_BUF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_buf_s {
  uint8_t* data;
  size_t size;
} a0_buf_t;

#ifdef __cplusplus
}
#endif

#endif  // A0_BUF_H
