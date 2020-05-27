#ifndef A0_COMMON_H
#define A0_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int errno_t;

static const errno_t A0_OK = 0;

typedef struct a0_buf_s {
  uint8_t* ptr;
  size_t size;
} a0_buf_t;

typedef a0_buf_t a0_arena_t;

typedef struct a0_callback_s {
  void* user_data;
  void (*fn)(void* user_data);
} a0_callback_t;

// My kingdom for a constexpr.
#define A0_UUID_SIZE 37
typedef char a0_uuid_t[A0_UUID_SIZE];

#ifdef __cplusplus
#define A0_NONE \
  {}
#else
#define A0_NONE \
  { 0 }
#endif

#define A0_LIKELY(x) __builtin_expect((x), 1)
#define A0_UNLIKELY(x) __builtin_expect((x), 0)

#define A0_STATIC_INLINE static inline __attribute__((always_inline))

#ifdef __cplusplus
}
#endif

#endif  // A0_COMMON_H
