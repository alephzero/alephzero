#include "once.h"

#include <a0/callback.h>
#include <a0/err.h>
#include <a0/inline.h>

#include <pthread.h>
#include <threads.h>

#include "err_macro.h"

thread_local a0_callback_t a0_pthread_once_ctx;

A0_STATIC_INLINE
void a0_once_do() {
  a0_callback_call(a0_pthread_once_ctx);
}

a0_err_t a0_once(pthread_once_t* flag, a0_callback_t callback) {
  a0_pthread_once_ctx = callback;
  A0_RETURN_SYSERR_ON_MINUS_ONE(pthread_once(flag, a0_once_do));
  return A0_OK;
}
