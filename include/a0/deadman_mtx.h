#ifndef A0_DEADMAN_MTX_H
#define A0_DEADMAN_MTX_H

#include <a0/err.h>
#include <a0/mtx.h>
#include <a0/tid.h>
#include <a0/time.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A shared token used by a0_deadman_mtx_t to track the state of a deadman.
// This token is designed for use in IPC. It is robust and death of
// the thread or process will automatically unlock the deadman.
typedef struct a0_deadman_mtx_shared_token_s {
  a0_mtx_t _mtx;
  uint64_t _tkn;
} a0_deadman_mtx_shared_token_t;

// A deadman mutex is similar to a mutex that supports waiting on lock/unlocked
// without acquiring. It also supports tracking a specific lock occurance,
// knowning whether the lock is held by the same owner as when previously
// queried.
//
// This object is thread/process specific and uses a shared token to track
// the state of the deadman across threads/processes.
typedef struct a0_deadman_mtx_s {
  a0_deadman_mtx_shared_token_t* _stkn;
  bool _shutdown;
  bool _inop;
  bool _is_owner;
} a0_deadman_mtx_t;

// ...
typedef struct a0_deadman_mtx_state_s {
  bool is_locked;
  bool is_owner;
  a0_tid_t owner_tid;
  uint64_t tkn;
} a0_deadman_mtx_state_t;

// Initialize a deadman mutex using a shared token.
a0_err_t a0_deadman_mtx_init(a0_deadman_mtx_t*, a0_deadman_mtx_shared_token_t*);

// Interrupts active lock/wait operations.
//
// This is intended to be used from another thread, since the lock/wait
// operation blocks the original thread.
//
// Does not unlock the mutex.
a0_err_t a0_deadman_mtx_shutdown(a0_deadman_mtx_t*);

// Lock the deadman mutex.
// On success returns A0_OK or A0_SYSERR(EOWNERDEAD).
a0_err_t a0_deadman_mtx_lock(a0_deadman_mtx_t*);
a0_err_t a0_deadman_mtx_trylock(a0_deadman_mtx_t*);
a0_err_t a0_deadman_mtx_timedlock(a0_deadman_mtx_t*, a0_time_mono_t*);

// Unlock the deadman mutex.
a0_err_t a0_deadman_mtx_unlock(a0_deadman_mtx_t*);

// Wait for someone to lock the deadman mutex.
// Returns a token that can be used to track the current owner.
a0_err_t a0_deadman_mtx_wait_locked(
    a0_deadman_mtx_t*,
    uint64_t* out_tkn);
a0_err_t a0_deadman_mtx_timedwait_locked(
    a0_deadman_mtx_t*, a0_time_mono_t*,
    uint64_t* out_tkn);

// Wait for the deadman to be unlocked.
// Uses the token from wait_locked to detect that the particular owner is unlocked.
a0_err_t a0_deadman_mtx_wait_unlocked(a0_deadman_mtx_t*, uint64_t);
a0_err_t a0_deadman_mtx_timedwait_unlocked(a0_deadman_mtx_t*, a0_time_mono_t*, uint64_t);

// ...
// This is inherently racy when querying external owner state.
// By the time this function returns, the owner may have changed or died.
a0_err_t a0_deadman_mtx_state(a0_deadman_mtx_t*, a0_deadman_mtx_state_t* out_state);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_MTX_H
