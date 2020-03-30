#ifndef A0_SHM_SYNC_H
#define A0_SHM_SYNC_H

#include <a0/common.h>

#ifdef __cplusplus
extern "C" {
#endif

// Futex.
// Operations rely on the address.
// It should not be copied or moved.
typedef uint32_t a0_ftx_t;

typedef struct a0_mtx_s a0_mtx_t;

// Mutex implementation designed for IPC.
//
// Similar to pthread_mutex_t with the following flags fixed:
// * Process shared.
// * Robust.
// * Error checking.
//
// "Inherits" from robust_list, which requires:
// * The first field MUST be a next pointer.
// * There must be a futex.
struct a0_mtx_s {
  a0_mtx_t* next;
  a0_mtx_t* prev;
  a0_ftx_t ftx;

  // TODO: Reconsider these fields.
  int32_t waiters;
  int32_t count;
};

errno_t a0_mtx_init(a0_mtx_t*);
errno_t a0_mtx_lock(a0_mtx_t*);
errno_t a0_mtx_trylock(a0_mtx_t*);
errno_t a0_mtx_consistent(a0_mtx_t*);
errno_t a0_mtx_unlock(a0_mtx_t*);

// TODO:
//
// typedef a0_ftx_t a0_cnd_t;
//
// errno_t a0_cnd_init(a0_cnd_t*);
// errno_t a0_cnd_wait(a0_cnd_t*, a0_mtx_t*);
// errno_t a0_cnd_signal(a0_cnd_t*, a0_mtx_t*);
// errno_t a0_cnd_broadcast(a0_cnd_t*, a0_mtx_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SHM_SYNC_H
