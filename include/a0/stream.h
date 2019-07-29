#ifndef A0_STREAM_H
#define A0_STREAM_H

#include <a0/common.h>
#include <a0/packet.h>
#include <a0/shmobj.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_stream_construct_options_s a0_stream_construct_options_t;
typedef struct a0_stream_options_s a0_stream_options_t;
typedef struct a0_stream_s a0_stream_t;

struct a0_stream_construct_options_s {
  uint64_t protocol_metadata_size;
  void (*on_construct)(a0_stream_t*);
  void (*on_already_constructed)(a0_stream_t*);
};

// Note: This object should not be copied or moved.
struct a0_stream_s {
  // Required from user.
  a0_shmobj_t* shmobj;

  // Optional from user.
  void* user_data;

  // Private.
  uint64_t _seq;
  uint64_t _off;
  bool _running;
  uint32_t _fu_await_cnt;
};

typedef struct a0_stream_frame_hdr_s {
  uint64_t seq;
} a0_stream_frame_hdr_t;

typedef struct a0_stream_frame_s {
  a0_stream_frame_hdr_t hdr;
  a0_buf_t data;
} a0_stream_frame_t;

errno_t a0_stream_init(a0_stream_t*, a0_stream_construct_options_t*);
errno_t a0_stream_close(a0_stream_t*);

typedef struct a0_locked_stream_s {
  a0_stream_t* stream;
} a0_locked_stream_t;

errno_t a0_lock_stream(a0_stream_t*, a0_locked_stream_t* lk_out);
errno_t a0_unlock_stream(a0_locked_stream_t);

// Caller does NOT own `out->base` and should not clean it up!
errno_t a0_stream_protocol_metadata(a0_locked_stream_t, a0_buf_t* out);

errno_t a0_stream_empty(a0_locked_stream_t, bool*);
errno_t a0_stream_nonempty(a0_locked_stream_t, bool*);
errno_t a0_stream_jump_head(a0_locked_stream_t);
errno_t a0_stream_jump_tail(a0_locked_stream_t);  // Inclusive.
errno_t a0_stream_has_next(a0_locked_stream_t, bool*);
errno_t a0_stream_next(a0_locked_stream_t);

// Await until the given predicate is satisfied.
// The predicate is checked when an event occurs on any stream wrapping shmobj.
errno_t a0_stream_await(a0_locked_stream_t,
                        errno_t (*pred)(a0_locked_stream_t, bool*));

// Caller does NOT own `frame_out->data` and should not clean it up!
errno_t a0_stream_frame(a0_locked_stream_t, a0_stream_frame_t* frame_out);

// Caller does NOT own `frame_out->data` and should not clean it up!
//
// For robustness, allocated frames are not tracked until explicitly commited.
// Note: if an alloc evicts an old frame, that frame is lost, even if no
// commit call is issued.
errno_t a0_stream_alloc(a0_locked_stream_t,
                        size_t,
                        a0_stream_frame_t* frame_out);
errno_t a0_stream_commit(a0_locked_stream_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_STREAM_H
