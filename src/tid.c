#include <a0/inline.h>
#include <a0/tid.h>

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <threads.h>
#include <unistd.h>

thread_local uint32_t _a0_tid = 0;
static pthread_once_t _a0_tid_reset_atfork_once;

A0_STATIC_INLINE
void a0_tid_reset() {
  _a0_tid = 0;
}

A0_STATIC_INLINE
void a0_tid_reset_atfork() {
  pthread_atfork(NULL, NULL, &a0_tid_reset);
}

uint32_t a0_tid() {
  if (!_a0_tid) {
    _a0_tid = syscall(SYS_gettid);
    pthread_once(&_a0_tid_reset_atfork_once, a0_tid_reset_atfork);
  }
  return _a0_tid;
}
