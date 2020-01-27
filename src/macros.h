#ifndef A0_INTERNAL_MACROS_H
#define A0_INTERNAL_MACROS_H

#include <errno.h>

#define A0_CAT(a, b) A0_CAT_(a, b)
#define A0_CAT_(a, b) a##b

#define A0_LIKELY(x) __builtin_expect((x), 1)
#define A0_UNLIKELY(x) __builtin_expect((x), 0)

#define A0_STATIC_INLINE static inline __attribute__((always_inline))

#define A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {                \
    return errno;                              \
  }

#define A0_INTERNAL_CLEANUP_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {             \
    goto cleanup;                           \
  }

#define A0_INTERNAL_RETURN_ERR_ON_ERR(x)                  \
  errno_t A0_CAT(_a0_var_, __LINE__) = (x);               \
  if (A0_UNLIKELY(A0_CAT(_a0_var_, __LINE__) != A0_OK)) { \
    return A0_CAT(_a0_var_, __LINE__);                    \
  }

// clang-format off
#if defined(__SANITIZE_THREAD__)
  #define A0_TSAN_ENABLED
#elif defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    #define A0_TSAN_ENABLED
  #endif
#endif

#ifdef A0_TSAN_ENABLED
  #ifdef _cplusplus
  extern "C" {
  #endif
    void AnnotateHappensBefore(const char*, int, void*);
    void AnnotateHappensAfter(const char*, int, void*);
  #ifdef _cplusplus
  }  // extern "C"
  #endif

  #define A0_TSAN_ANNOTATE_HAPPENS_BEFORE(addr) \
    AnnotateHappensBefore(__FILE__, __LINE__, (void*)(addr))
  #define A0_TSAN_ANNOTATE_HAPPENS_AFTER(addr) \
    AnnotateHappensAfter(__FILE__, __LINE__, (void*)(addr))
#else
  #define A0_TSAN_ANNOTATE_HAPPENS_BEFORE(addr)
  #define A0_TSAN_ANNOTATE_HAPPENS_AFTER(addr)
#endif
// clang-format on

#endif  // A0_INTERNAL_MACROS_H
