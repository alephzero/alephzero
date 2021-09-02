#ifndef A0_ERR_H
#define A0_ERR_H

#include <a0/empty.h>

#include <threads.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum a0_err_e {
  A0_ERRCODE_OK = 0,
  A0_ERRCODE_SYSERR = 1,
  A0_ERRCODE_CUSTOM_MSG = 2,
  A0_ERRCODE_INVALID_ARG = 3,
  A0_ERRCODE_DONE_ITER = 4,
  A0_ERRCODE_NOT_FOUND = 5,
  A0_ERRCODE_BAD_TOPIC = 6,
  A0_ERRCODE_TRANSPORT_FRAME_GT_ARENA = 7,
  A0_ERRCODE_TRANSPORT_CANNOT_MOVE_POINTER = 8,
} a0_err_t;

static const a0_err_t A0_OK = A0_EMPTY;

extern thread_local int a0_err_syscode;
extern thread_local char a0_err_msg[1024];

const char* a0_strerror(a0_err_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_ERR_H
