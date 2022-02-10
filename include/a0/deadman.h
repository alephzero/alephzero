#ifndef A0_DEADMAN_H
#define A0_DEADMAN_H

#include <a0/deadman_mtx.h>
#include <a0/file.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_deadman_topic_s {
  const char* name;
} a0_deadman_topic_t;

typedef struct a0_deadman_s {
  a0_file_t _file;
  a0_deadman_mtx_t* _deadman_mtx;
  bool _is_owner;
} a0_deadman_t;

a0_err_t a0_deadman_init(a0_deadman_t*, a0_deadman_topic_t);
a0_err_t a0_deadman_close(a0_deadman_t*);

a0_err_t a0_deadman_take(a0_deadman_t*);
a0_err_t a0_deadman_trytake(a0_deadman_t*);
a0_err_t a0_deadman_timedtake(a0_deadman_t*, a0_time_mono_t*);
a0_err_t a0_deadman_release(a0_deadman_t*);
a0_err_t a0_deadman_wait_taken(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedwait_taken(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_wait_released(a0_deadman_t*, uint64_t tkn);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t*, uint64_t tkn);
a0_err_t a0_deadman_state(a0_deadman_t*, bool* out_istaken, uint64_t* out_tkn);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_H
