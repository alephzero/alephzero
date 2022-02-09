#ifndef A0_DEADMAN_H
#define A0_DEADMAN_H

#include <a0/mtx.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// A deadman synchronization primitive designed to be robust in shared-memory.
//
// A deadman can only be held by one thread at a time.
// The deadman is not recursive.
// Death of the thread or process will automatically release the deadman.
typedef struct a0_deadman_s {
  // Guard protects the token and acquired bit.
  a0_mtx_t _guard;
  // Condition variable on new acquisitions.
  a0_cnd_t _acquire_cnd;
  // The owner's mutex. This is used to track the owner death.
  a0_mtx_t _owner_mtx;
  // The current owner's unique id.
  uint64_t _tkn;
  // In combination with the owner's mutex, this is used to track whether
  // the deadman is acquired.
  bool _acquired;
} a0_deadman_t;

// Acquire the deadman.
// On success returns A0_OK or A0_SYSERR(EOWNERDEAD).
a0_err_t a0_deadman_acquire(a0_deadman_t*);
a0_err_t a0_deadman_tryacquire(a0_deadman_t*);
a0_err_t a0_deadman_timedacquire(a0_deadman_t*, a0_time_mono_t*);

// Release the deadman.
a0_err_t a0_deadman_release(a0_deadman_t*);

// Wait for someone to acquire the deadman.
// Returns a token that can be used to track the current deadman owner.
a0_err_t a0_deadman_wait_acquired(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedwait_acquired(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);

// Wait for the deadman to be released.
// Uses the token from wait_acquired to detect that the particular instance is released.
a0_err_t a0_deadman_wait_released(a0_deadman_t*, uint64_t tkn);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t*, uint64_t tkn);

// Queries whether the deadman is currently acquired and returns the token.
a0_err_t a0_deadman_isacquired(a0_deadman_t*, bool*, uint64_t* out_tkn);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_H
