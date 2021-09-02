#include <a0/err.h>

#include <errno.h>
#include <string.h>

thread_local int a0_err_syscode;
thread_local char a0_err_msg[1024];

const char* a0_strerror(a0_err_t err) {
  switch (err) {
    case A0_ERRCODE_OK: {
      return strerror(0);
    }
    case A0_ERRCODE_SYSERR: {
      return strerror(a0_err_syscode);
    }
    case A0_ERRCODE_CUSTOM_MSG: {
      return a0_err_msg;
    }
    case A0_ERRCODE_INVALID_ARG: {
      return strerror(EINVAL);
    }
    case A0_ERRCODE_DONE_ITER: {
      return "Done iterating";
    }
    case A0_ERRCODE_NOT_FOUND: {
      return "Element not found";
    }
    case A0_ERRCODE_BAD_TOPIC: {
      return "Invalid topic name";
    }
    case A0_ERRCODE_TRANSPORT_FRAME_GT_ARENA: {
      return "Transport cannot allocate frame larger than arena";
    }
    case A0_ERRCODE_TRANSPORT_CANNOT_MOVE_POINTER: {
      return "Transport cannot move pointer";
    }
    default: {
      break;
    }
  }
  return "";
}
