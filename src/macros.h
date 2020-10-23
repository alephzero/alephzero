#ifndef A0_SRC_MACROS_H
#define A0_SRC_MACROS_H

#include <errno.h>

#ifdef DEBUG

#include <stdio.h>

#define A0_ASSERT(X, MSG, ...)                           \
  ({                                                     \
    _Pragma("GCC diagnostic push");                      \
    _Pragma("GCC diagnostic ignored \"-Wparentheses\""); \
    (X) ?: ({                                            \
      fprintf(stderr, "AlephZero Assertion Failed!\n");  \
      fprintf(stderr, "File: %s\n", __FILE__);           \
      fprintf(stderr, "Line: %d\n", __LINE__);           \
      fprintf(stderr, "Func: %s\n", __func__);           \
      fprintf(stderr, "Expr: %s\n", #X);                 \
      fprintf(stderr, "Msg:  " MSG "\n", ##__VA_ARGS__); \
      abort();                                           \
      (X);                                               \
    });                                                  \
    _Pragma("GCC diagnostic pop");                       \
  })

#define A0_ASSERT_OK(ERR, MSG, ...)                           \
  ({                                                          \
    errno_t err = (ERR);                                      \
    if (A0_UNLIKELY(err)) {                                   \
      fprintf(stderr, "AlephZero Assertion Failed!\n");       \
      fprintf(stderr, "File: %s\n", __FILE__);                \
      fprintf(stderr, "Line: %d\n", __LINE__);                \
      fprintf(stderr, "Func: %s\n", __func__);                \
      fprintf(stderr, "Expr: %s\n", #ERR);                    \
      fprintf(stderr, "Err:  [%d] %s\n", err, strerror(err)); \
      fprintf(stderr, "Msg:  " MSG "\n", ##__VA_ARGS__);      \
      abort();                                                \
    }                                                         \
    err;                                                      \
  })

#else

#define A0_ASSERT(X, ...) ({ (X); })

#define A0_ASSERT_OK(ERR, ...) ({ (ERR); })

#endif

#define A0_LIKELY(x) __builtin_expect((x), 1)
#define A0_UNLIKELY(x) __builtin_expect((x), 0)

#define A0_STATIC_INLINE static inline __attribute__((always_inline))

#define A0_CAT(a, b) A0_CAT_(a, b)
#define A0_CAT_(a, b) a##b

#define A0_RETURN_ERR_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {       \
    return errno;                     \
  }

#define A0_RETURN_ERR_ON_ERR(x)                           \
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

#endif  // A0_SRC_MACROS_H
