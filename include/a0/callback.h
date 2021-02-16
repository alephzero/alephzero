#ifndef A0_CALLBACK_H
#define A0_CALLBACK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_callback_s {
  void* user_data;
  void (*fn)(void* user_data);
} a0_callback_t;

#ifdef __cplusplus
}
#endif

#endif  // A0_CALLBACK_H
