#ifndef A0_MTX_H
#define A0_MTX_H

#include <a0/err.h>
#include <a0/time.h>
#include <a0/unused.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t a0_ftx_t;

// https://stackoverflow.com/questions/61645966/is-typedef-allowed-before-definition
struct a0_mtx_s;

typedef struct a0_mtx_s a0_mtx_t;

// Mutex implementation designed for IPC.
//
// Similar to pthread_mutex_t with the following flags fixed:
// * Process shared.
// * Robust.
// * Error checking.
// * Priority inheriting.
//
// Unlike pthread_mutex_t:
// * timespec are expected to use CLOCK_BOOTTIME.
// * The protected resource is expected to be consistent independently.
//   pthread expects pthread_mutex_consistent to be called after a lock
//   returns EOWNERDEAD. Failing to do so may result in ENOTRECOVERABLE.
//
// A successful lock returns A0_OK or A0_SYSERR(lock) == EOWNERDEAD.
//
// struct a0_mtx_s "Inherits" from robust_list, which requires:
// * The first field MUST be a next pointer.
// * There must be a futex, which makes the mutex immovable.
//
// Note: a mutex MUST be unlocked before being freed or unmapped.
struct a0_mtx_s {
  a0_mtx_t* next;
  a0_mtx_t* prev;
  a0_ftx_t ftx;
};

a0_err_t a0_mtx_lock(a0_mtx_t*) A0_WARN_UNUSED_RESULT;
a0_err_t a0_mtx_timedlock(a0_mtx_t*, a0_time_mono_t*) A0_WARN_UNUSED_RESULT;
a0_err_t a0_mtx_trylock(a0_mtx_t*) A0_WARN_UNUSED_RESULT;
a0_err_t a0_mtx_unlock(a0_mtx_t*);

// Returns true if the mutex is locked, regardless of whether the previous owner died.
bool a0_mtx_lock_successful(a0_err_t);
// Returns true if the mutex is locked and the previous owner died.
bool a0_mtx_previous_owner_died(a0_err_t);

typedef a0_ftx_t a0_cnd_t;

a0_err_t a0_cnd_wait(a0_cnd_t*, a0_mtx_t*);
a0_err_t a0_cnd_timedwait(a0_cnd_t*, a0_mtx_t*, a0_time_mono_t*);
a0_err_t a0_cnd_signal(a0_cnd_t*, a0_mtx_t*);
a0_err_t a0_cnd_broadcast(a0_cnd_t*, a0_mtx_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_MTX_H
