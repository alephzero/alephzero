#ifndef A0_DEADMAN_MTX_H
#define A0_DEADMAN_MTX_H

#include <a0/mtx.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A deadman mutex is an extended mutex that supports waiting on lock/unlocked
// without acquiring. It also supports tracking a specific lock occurance,
// knowning whether the lock is held by the same owner as when previously
// queried.
//
// The deadman mutex is designed for use in IPC. It is robust and death of
// the thread or process will automatically unlock the deadman.
typedef struct a0_deadman_mtx_s {
  // Guard protects the token and locked bit.
  a0_mtx_t _guard;
  // Condition variable on new locks.
  a0_cnd_t _lock_cnd;
  // The owner's mutex. This is used to track the owner death.
  a0_mtx_t _owner_mtx;
  // The current owner's unique id.
  uint64_t _tkn;
  // In combination with the owner's mutex, this is used to track whether
  // the deadman is locked.
  bool _locked;
} a0_deadman_mtx_t;

// Lock the deadman mutex.
// On success returns A0_OK or A0_SYSERR(EOWNERDEAD).
a0_err_t a0_deadman_mtx_lock(a0_deadman_mtx_t*);
a0_err_t a0_deadman_mtx_trylock(a0_deadman_mtx_t*);
a0_err_t a0_deadman_mtx_timedlock(a0_deadman_mtx_t*, a0_time_mono_t*);

// Unlock the deadman mutex.
a0_err_t a0_deadman_mtx_unlock(a0_deadman_mtx_t*);

// Wait for someone to lock the deadman mutex.
// Returns a token that can be used to track the current owner.
a0_err_t a0_deadman_mtx_wait_locked(a0_deadman_mtx_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_mtx_timedwait_locked(a0_deadman_mtx_t*, a0_time_mono_t*, uint64_t* out_tkn);

// Wait for the deadman to be unlocked.
// Uses the token from wait_locked to detect that the particular instance is unlocked.
a0_err_t a0_deadman_mtx_wait_unlocked(a0_deadman_mtx_t*, uint64_t tkn);
a0_err_t a0_deadman_mtx_timedwait_unlocked(a0_deadman_mtx_t*, a0_time_mono_t*, uint64_t tkn);

// Queries whether the deadman mutex is currently locked. If yes, returns the owner's token.
a0_err_t a0_deadman_mtx_state(a0_deadman_mtx_t*, bool* out_islocked, uint64_t* out_tkn);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_MTX_H
