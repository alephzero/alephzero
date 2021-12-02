#ifndef A0_RWMTX_H
#define A0_RWMTX_H

#include <a0/callback.h>
#include <a0/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

// A reader-writer mutex designed to be robust in shared-memory.
//
// The robustness requirement prevents us from having an unbound number of
// readers in O(1) space, since there's no mechanism by which we can decrement
// a value on process death. Instead, we create an explicit slot for each
// reader and use the robustness mechanism linux provides to normal mutex.
//
// To use a0_rwmtx_t, you must also allocate an array of a0_mtx_t, which is
// refered to as the rmtx array.
// The rmtx array MUST be the same for all operations for the lifetime of
// a0_rwmtx_t. The rmtx array MAY NOT be shared across a0_rwmtx_t.
// Both a0_rwmtx_t and the rmtx array must be zeroed out to be initialized.
//
// Unlike a0_mtx_t, a0_rwmtx_t does not aim to match the api of the pthread
// equivalent. Also, unlike a0_mtx_t, a0_rwmtx_t does not detect deadlock or
// notify the user of potential consistency issues if the prior owner was
// found to have died.
typedef struct a0_rwmtx_s {
  // guard protects changes to the internal state of the rwmtx.
  a0_mtx_t guard;
  // cnd is used to wait on changes, blocked by guard.
  a0_cnd_t cnd;
  // wmtx is the exclusive write mutex.
  a0_mtx_t wmtx;

  // _next_rmtx_idx is an internal accounting variable, used to speed up operations.
  size_t _next_rmtx_idx;
} a0_rwmtx_t;

// Span of reader mutex array available for a reader-writer mutex.
typedef struct a0_rwmtx_rmtx_span_s {
  // Array pointer.
  a0_mtx_t* arr;
  // Number of reader slots.
  size_t size;
} a0_rwmtx_rmtx_span_t;

// Token emitted by a locking operation.
// Used to efficiently unlock.
typedef struct a0_rwmtx_tkn_s {
  a0_mtx_t* mtx;
} a0_rwmtx_tkn_t;

a0_err_t a0_rwmtx_rlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_rwmtx_tkn_t*);
a0_err_t a0_rwmtx_wlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_rwmtx_tkn_t*);

a0_err_t a0_rwmtx_tryrlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_rwmtx_tkn_t*);
a0_err_t a0_rwmtx_trywlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_rwmtx_tkn_t*);

a0_err_t a0_rwmtx_timedrlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_time_mono_t, a0_rwmtx_tkn_t*);
a0_err_t a0_rwmtx_timedwlock(a0_rwmtx_t*, a0_rwmtx_rmtx_span_t, a0_time_mono_t, a0_rwmtx_tkn_t*);

a0_err_t a0_rwmtx_unlock(a0_rwmtx_t*, a0_rwmtx_tkn_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_RWMTX_H
