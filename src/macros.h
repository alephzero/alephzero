#ifndef A0_SRC_MACROS_H
#define A0_SRC_MACROS_H

#include <errno.h>

#define A0_CAT(a, b) A0_CAT_(a, b)
#define A0_CAT_(a, b) a##b

#ifdef DEBUG

#define A0_ASSERT(ERR, MSG, ...)                                                                        \
  ({                                                                                                    \
    errno_t err = (ERR);                                                                                \
    if (A0_UNLIKELY(err)) {                                                                             \
      fprintf(stderr, "%s:%d: errno: %s] " MSG "\n", __FILE__, __LINE__, strerror(ERR), ##__VA_ARGS__); \
      abort();                                                                                          \
    }                                                                                                   \
    err;                                                                                                \
  })

#define A0_ASSERT_RETURN(ERR, MSG, ...)                                                                 \
  ({                                                                                                    \
    errno_t err = (ERR);                                                                                \
    if (A0_UNLIKELY(err)) {                                                                             \
      fprintf(stderr, "%s:%d: errno: %s] " MSG "\n", __FILE__, __LINE__, strerror(ERR), ##__VA_ARGS__); \
      abort();                                                                                          \
    }                                                                                                   \
    return err;                                                                                         \
  })

#else

#define A0_ASSERT(ERR, ...) ({ (ERR); })

#define A0_ASSERT_RETURN(ERR, ...) \
  ({                               \
    errno_t err = (ERR);           \
    if (A0_UNLIKELY(err)) {        \
      return err;                  \
    }                              \
  })

#endif

#define A0_RETURN_ERR_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {       \
    return errno;                     \
  }

#define A0_CLEANUP_ON_MINUS_ONE(x) \
  if (A0_UNLIKELY((x) == -1)) {    \
    goto cleanup;                  \
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
