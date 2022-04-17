#ifndef A0_SRC_TSAN_H
#define A0_SRC_TSAN_H

#ifdef __cplusplus
extern "C" {
#endif

// TSAN is worth the pain of properly annotating our mutex.

// clang-format off
#if defined(__SANITIZE_THREAD__)
  #define A0_TSAN_ENABLED
#elif defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    #define A0_TSAN_ENABLED
  #endif
#endif

#ifdef A0_TSAN_ENABLED
  #define A0_NO_TSAN __attribute__((no_sanitize("thread")))
#else
  #define A0_NO_TSAN
#endif

#ifdef A0_TSAN_ENABLED
  extern void AnnotateHappensBefore(const char*, int, void*);
  extern void AnnotateHappensAfter(const char*, int, void*);
  #define A0_TSAN_HAPPENS_BEFORE(addr) \
    AnnotateHappensBefore(__FILE__, __LINE__, (void*)(addr))
  #define A0_TSAN_HAPPENS_AFTER(addr) \
    AnnotateHappensAfter(__FILE__, __LINE__, (void*)(addr))
#else
  #define A0_TSAN_HAPPENS_BEFORE(addr)
  #define A0_TSAN_HAPPENS_AFTER(addr)
#endif
// clang-format on

static const unsigned __tsan_mutex_linker_init = 1 << 0;
static const unsigned __tsan_mutex_write_reentrant = 1 << 1;
static const unsigned __tsan_mutex_read_reentrant = 1 << 2;
static const unsigned __tsan_mutex_not_static = 1 << 8;
static const unsigned __tsan_mutex_read_lock = 1 << 3;
static const unsigned __tsan_mutex_try_lock = 1 << 4;
static const unsigned __tsan_mutex_try_lock_failed = 1 << 5;
static const unsigned __tsan_mutex_recursive_lock = 1 << 6;
static const unsigned __tsan_mutex_recursive_unlock = 1 << 7;

#ifdef A0_TSAN_ENABLED

void __tsan_mutex_create(void* addr, unsigned flags);
void __tsan_mutex_destroy(void* addr, unsigned flags);
void __tsan_mutex_pre_lock(void* addr, unsigned flags);
void __tsan_mutex_post_lock(void* addr, unsigned flags, int recursion);
int __tsan_mutex_pre_unlock(void* addr, unsigned flags);
void __tsan_mutex_post_unlock(void* addr, unsigned flags);
void __tsan_mutex_pre_signal(void* addr, unsigned flags);
void __tsan_mutex_post_signal(void* addr, unsigned flags);
void __tsan_mutex_pre_divert(void* addr, unsigned flags);
void __tsan_mutex_post_divert(void* addr, unsigned flags);

#else

#define _u_ __attribute__((unused))

A0_STATIC_INLINE void _u_ __tsan_mutex_create(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_destroy(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_lock(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_lock(_u_ void* addr,
                                                 _u_ unsigned flags,
                                                 _u_ int recursion) {}
A0_STATIC_INLINE int _u_ __tsan_mutex_pre_unlock(_u_ void* addr, _u_ unsigned flags) {
  return 0;
}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_unlock(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_signal(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_signal(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_divert(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_divert(_u_ void* addr, _u_ unsigned flags) {}

#endif

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_TSAN_H
