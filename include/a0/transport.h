#ifndef A0_TRANSPORT_H
#define A0_TRANSPORT_H

#include <a0/alloc.h>
#include <a0/common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_transport_s {
  a0_arena_t _arena;

  // Connection pointer info.
  uint64_t _seq;
  uint64_t _off;

  // Number of active awaits using this transport connection.
  // TODO(lshamis): Could this be a bool?
  uint32_t _await_cnt;

  // Whether the transport is in the process of disconnecting.
  bool _closing;

  // Whether the transport has an unflushed notification.
  bool _should_notify;

  // Unique token used to distinguish locks.
  uint32_t _lk_tkn;
} a0_transport_t;

typedef struct a0_transport_frame_hdr_s {
  uint64_t seq;
  uint64_t off;
  uint64_t next_off;
  uint64_t prev_off;
  uint64_t data_size;
} a0_transport_frame_hdr_t;

typedef struct a0_transport_frame_s {
  a0_transport_frame_hdr_t hdr;
  uint8_t* data;
} a0_transport_frame_t;

typedef enum a0_transport_init_status_s {
  A0_TRANSPORT_CREATED,
  A0_TRANSPORT_CONNECTED,
} a0_transport_init_status_t;

// Wrapper around a transport, used to "strongly" type unique-access.
typedef struct a0_locked_transport_s {
  a0_transport_t* transport;
} a0_locked_transport_t;

errno_t a0_transport_init(a0_transport_t*,
                          a0_arena_t,
                          a0_transport_init_status_t* status_out,
                          a0_locked_transport_t* lk_out);

// Allocates space in the transport for metadata.
// This can only be called after a0_transport_init, but before any allocations.
// Once the transports start allocating frames, the metadata size is fixed.
errno_t a0_transport_init_metadata(a0_locked_transport_t, size_t metadata_size);

// Awakens all outstanding awaits. Future await attempts fail.
errno_t a0_transport_close(a0_transport_t*);

// Initializes the locked_transport object, wrapping the given transport.
errno_t a0_transport_lock(a0_transport_t*, a0_locked_transport_t* lk_out);
// The locked_transport object is invalid after unlock.
errno_t a0_transport_unlock(a0_locked_transport_t);

// Caller does NOT own `metadata_out->ptr` and should not clean it up!
errno_t a0_transport_metadata(a0_locked_transport_t, a0_buf_t* metadata_out);

errno_t a0_transport_empty(a0_locked_transport_t, bool*);
errno_t a0_transport_nonempty(a0_locked_transport_t, bool*);
errno_t a0_transport_ptr_valid(a0_locked_transport_t, bool*);
errno_t a0_transport_jump_head(a0_locked_transport_t);
errno_t a0_transport_jump_tail(a0_locked_transport_t);  // Inclusive.
errno_t a0_transport_has_next(a0_locked_transport_t, bool*);
errno_t a0_transport_next(a0_locked_transport_t);
errno_t a0_transport_has_prev(a0_locked_transport_t, bool*);
errno_t a0_transport_prev(a0_locked_transport_t);

// Await until the given predicate is satisfied.
// The predicate is checked when an event occurs on any transport wrapping shm.
// TODO(lshamis): should pred take user_data?
errno_t a0_transport_await(a0_locked_transport_t, errno_t (*pred)(a0_locked_transport_t, bool*));

// Caller does NOT own `frame_out->data` and should not clean it up!
errno_t a0_transport_frame(a0_locked_transport_t, a0_transport_frame_t* frame_out);

// Caller does NOT own `frame_out->data` and should not clean it up!
//
// For robustness, allocated frames are not tracked until explicitly commited.
// Note: if an alloc evicts an old frame, that frame is lost, even if no
// commit call is issued.
errno_t a0_transport_alloc(a0_locked_transport_t, size_t, a0_transport_frame_t* frame_out);
// Whether an alloc would evict.
errno_t a0_transport_alloc_evicts(a0_locked_transport_t, size_t, bool*);
errno_t a0_transport_allocator(a0_locked_transport_t*, a0_alloc_t*);
errno_t a0_transport_commit(a0_locked_transport_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
