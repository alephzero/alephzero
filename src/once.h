#ifndef A0_SRC_ONCE_H
#define A0_SRC_ONCE_H

#include <a0/callback.h>
#include <a0/err.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

a0_err_t a0_once(pthread_once_t*, a0_callback_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_ONCE_H
