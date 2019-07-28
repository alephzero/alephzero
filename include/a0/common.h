#ifndef A0_COMMON_H
#define A0_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef int errno_t;

static const errno_t A0_OK = 0;

typedef struct a0_buf_s {
  uint8_t* ptr;
  size_t size;
} a0_buf_t;

typedef struct a0_alloc_s a0_alloc_t;

struct a0_alloc_s {
  void* user_data;
  void (*callback)(void* user_data, size_t size, a0_buf_t* buf);
};

#endif  // A0_COMMON_H
