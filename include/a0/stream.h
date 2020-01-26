#ifndef A0_STREAM_H
#define A0_STREAM_H

#include <a0/common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_stream_s {
  a0_buf_t _arena;

  // Connection pointer info.
  uint64_t _seq;
  uint64_t _off;

  // Number of active awaits using this stream connection.
  // TODO: Could this be a bool?
  uint32_t _await_cnt;

  // Whether the stream is in the process of disconnecting.
  bool _closing;

  // Whether the stream has an unflushed notification.
  bool _should_notify;

  // Unique token used to distinguish locks.
  uint32_t _lk_tkn;
} a0_stream_t;

typedef struct a0_stream_protocol_s {
  a0_buf_t name;
  uint32_t major_version;
  uint32_t minor_version;
  uint32_t patch_version;
  uint64_t metadata_size;
} a0_stream_protocol_t;

typedef struct a0_stream_frame_hdr_s {
  uint64_t seq;
  uint64_t off;
  uint64_t next_off;
  uint64_t prev_off;
  uint64_t data_size;
} a0_stream_frame_hdr_t;

typedef struct a0_stream_frame_s {
  a0_stream_frame_hdr_t hdr;
  uint8_t* data;
} a0_stream_frame_t;

typedef enum a0_stream_init_status_s {
  A0_STREAM_CREATED,
  A0_STREAM_PROTOCOL_MATCH,
  A0_STREAM_PROTOCOL_MISMATCH,
} a0_stream_init_status_t;

// Wrapper around a stream, used to "strongly" type unique-access.
typedef struct a0_locked_stream_s {
  a0_stream_t* stream;
} a0_locked_stream_t;

errno_t a0_stream_init(a0_stream_t*,
                       a0_buf_t arena,
                       a0_stream_protocol_t,
                       a0_stream_init_status_t* status_out,
                       a0_locked_stream_t* lk_out);
errno_t a0_stream_close(a0_stream_t*);

// Initializes the locked_stream object, wrapping the given stream.
errno_t a0_lock_stream(a0_stream_t*, a0_locked_stream_t* lk_out);
// The locked_stream object is invalid after unlock.
errno_t a0_unlock_stream(a0_locked_stream_t);

// Caller does NOT own `protocol_out->name->ptr` and should not clean it up!
// Caller does NOT own `metadata_out->ptr` and should not clean it up!
errno_t a0_stream_protocol(a0_locked_stream_t,
                           a0_stream_protocol_t* protocol_out,
                           a0_buf_t* metadata_out);

errno_t a0_stream_empty(a0_locked_stream_t, bool*);
errno_t a0_stream_nonempty(a0_locked_stream_t, bool*);
errno_t a0_stream_ptr_valid(a0_locked_stream_t, bool*);
errno_t a0_stream_jump_head(a0_locked_stream_t);
errno_t a0_stream_jump_tail(a0_locked_stream_t);  // Inclusive.
errno_t a0_stream_has_next(a0_locked_stream_t, bool*);
errno_t a0_stream_next(a0_locked_stream_t);
errno_t a0_stream_has_prev(a0_locked_stream_t, bool*);
errno_t a0_stream_prev(a0_locked_stream_t);

// Await until the given predicate is satisfied.
// The predicate is checked when an event occurs on any stream wrapping shm.
// TODO: should pred take user_data?
errno_t a0_stream_await(a0_locked_stream_t, errno_t (*pred)(a0_locked_stream_t, bool*));

// Caller does NOT own `frame_out->data` and should not clean it up!
errno_t a0_stream_frame(a0_locked_stream_t, a0_stream_frame_t* frame_out);

// Caller does NOT own `frame_out->data` and should not clean it up!
//
// For robustness, allocated frames are not tracked until explicitly commited.
// Note: if an alloc evicts an old frame, that frame is lost, even if no
// commit call is issued.
errno_t a0_stream_alloc(a0_locked_stream_t, size_t, a0_stream_frame_t* frame_out);
errno_t a0_stream_commit(a0_locked_stream_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_STREAM_H
