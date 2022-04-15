#include "robust.h"

#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/thread_local.h>

#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <syscall.h>
#include <unistd.h>

#include "atomic.h"

A0_THREAD_LOCAL bool a0_robust_init_flag = false;

A0_STATIC_INLINE
void a0_robust_reset() {
  a0_robust_init_flag = 0;
}

A0_STATIC_INLINE
void a0_robust_reset_atfork() {
  pthread_atfork(NULL, NULL, &a0_robust_reset);
}

static pthread_once_t a0_robust_reset_atfork_once;

typedef struct robust_list robust_list_t;
typedef struct robust_list_head robust_list_head_t;

A0_THREAD_LOCAL robust_list_head_t a0_robust_head;

A0_STATIC_INLINE
void a0_robust_init_head() {
  a0_robust_head.list.next = &a0_robust_head.list;
  a0_robust_head.futex_offset = offsetof(a0_mtx_t, ftx);
  a0_robust_head.list_op_pending = NULL;
  syscall(SYS_set_robust_list, &a0_robust_head.list, sizeof(a0_robust_head));
}

A0_STATIC_INLINE
void a0_robust_init_thread() {
  if (a0_robust_init_flag) {
    return;
  }

  pthread_once(&a0_robust_reset_atfork_once, a0_robust_reset_atfork);
  a0_robust_init_head();
  a0_robust_init_flag = true;
}

void a0_robust_op_start(a0_mtx_t* mtx) {
  a0_robust_init_thread();
  a0_robust_head.list_op_pending = (struct robust_list*)mtx;
  a0_barrier();
}

void a0_robust_op_end(a0_mtx_t* mtx) {
  (void)mtx;
  a0_barrier();
  a0_robust_head.list_op_pending = NULL;
}

A0_STATIC_INLINE
bool robust_is_head(a0_mtx_t* mtx) {
  return mtx == (a0_mtx_t*)&a0_robust_head;
}

void a0_robust_op_add(a0_mtx_t* mtx) {
  a0_mtx_t* old_first = (a0_mtx_t*)a0_robust_head.list.next;

  mtx->prev = (a0_mtx_t*)&a0_robust_head;
  mtx->next = old_first;

  a0_barrier();

  a0_robust_head.list.next = (robust_list_t*)mtx;
  if (!robust_is_head(old_first)) {
    old_first->prev = mtx;
  }
}

void a0_robust_op_del(a0_mtx_t* mtx) {
  a0_mtx_t* prev = mtx->prev;
  a0_mtx_t* next = mtx->next;
  prev->next = next;
  if (!robust_is_head(next)) {
    next->prev = prev;
  }
}
