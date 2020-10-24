/**
 * \file transport.h
 * \rst
 *
 * Overview
 * --------
 *
 * The core of AlephZero's offering is an interprocess-safe datastructure.
 *
 * The datastructure is, effectively, a circular linked-list, layed out within
 * a given arena. It can be thought of as a simple allocator.
 *
 * The transport holds a list of frames, where each frame is a contains a
 * user-provided byte string.
 *
 * The frames are layed out in the given arena, one after the other, max-aligned
 * in case the bytes needs to be reinterpreted as a struct.
 *
 * Once the arena is exhausted, and the next requested frame cannot be added without
 * overrunning the arena, the oldest frames will be evicted to make space.
 *
 * A transport has a single exclusive lock that must be acquired before reading or
 * writing frames. This is to prevent a frame from being erased while another process
 * is reading the frame. A general reader-writer-lock prevents consistency guarantees,
 * but we may allow for a fixed, limited, number of simultanious readers in the future.
 *
 * The layout of the transport is guaranteed to be consistent on the same machine,
 * regardless of libc implementations.
 *
 * The transport may also optionally reserves a section of it's space for user metadata.
 * Metadata space must be reserved before the first frame is written. Unlike the rest
 * of the transport, consistency of the metadata is not guaranteed. In general, it should
 * not be used for mutable state.
 *
 * Constructing
 * ------------
 *
 * A transport exists in an arena, a flat contiguous memory buffer.
 *
 * The constructor returns a status of CREATED or CONNECTED along with exclusive
 * ownership of the mutex. This is meant, in part, to allow the caller a chance
 * to make one-time preparations for the transport, such as set up the metadata.
 *
 * Accessing
 * ---------
 *
 * :cpp:struct:`transport <a0_transport_t>` keeps an external pointer into the arena
 * that is used to iterate through frames.
 *
 * To access frames within the arena, the transport must be locked. To help
 * prevent bugs associated with unlocked access, all access functions require a
 * a0_locked_transport_t object, returned by a0_transport_lock. The lock should
 * be freed with a0_transport_unlock.
 *
 * Since frames are organized in a linked-list format, iteration and access follows
 * from standard linked-list api.
 * You MUST begin by setting the pointer to an existing node via
 * a0_transport_jump_head or a0_transport_jump_tail.
 * Afterwards, you may proceed via a0_transport_prev and a0_transport_next.
 * To check if a previous or next exist, you may use the a0_transport_has_prev
 * and a0_transport_has_next.
 *
 * .. note::
 *
 *    a0_transport_next does not necessarily refer to the sequentially next. If
 *    the transport was exhasted and frames evicted, a0_transport_has_next refers
 *    to the existance of some frame added after the one currently pointed to.
 *
 *    If a0_transport_has_next returns true, then it will remain true across
 *    unlock-relock. That is not the case for a0_transport_has_prev.
 *
 * a0_transport_frame returns a frame pointer into the arena.
 *
 * If the transport is unlocked and relocked, the pointer may no longer be valid.
 * To check for validity, you can use a0_transport_ptr_valid. You can use
 * a0_transport_has_next and a0_transport_next regardless of whether the pointer
 * is still valid.
 *
 * Writing
 * -------
 *
 * To write a frame to the transport, we begin by allocating space with
 * a0_transport_alloc. This will return a frame pointing into the arena. Once
 * the frame written to, the user MUST call a0_transport_commit.
 *
 * There should only be one outstanding allocation before a commit. Multiple
 * allocations before a commit may result in lost sequence numbers based on
 * the current consistency model.
 *
 * Allocation may cause eviction, even if not committed. To see if an allocation
 * would cause an eviction, use a0_transport_alloc_evicts.
 *
 * Frame Structure
 * ---------------
 *
 * A frame is a simple container with a header and user provided byte string.
 * The header has a pointer to the next and previous element, as well as a
 * sequence number, and data size.
 *
 * Notifications
 * -------------
 *
 * The transport provides a simple condition-variable style wait/notify.
 * a0_transport_await atomically unlocks the transport and will be awoken when
 * a given predicate is satisfied.
 *
 * The predicate is checked immediately, then whenever the transport is unlocked
 * following a commit or eviction.
 *
 * Consistency
 * -----------
 *
 * The state of the transport is double-buffered and updated atomically during
 * a commit.
 *
 * The transport uses a robust lock that will detect if the owner dies and will
 * free the lock for the next user. Because of the double-buffered state, the
 * transport is always consistent.
 *
 * \endrst
 */

#ifndef A0_TRANSPORT_H
#define A0_TRANSPORT_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \addtogroup TRANSPORT
 *  @{
 */

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
  /// Sequence number.
  uint64_t seq;
  /// Offset within the arena.
  uint64_t off;
  /// Offset of the next frame.
  uint64_t next_off;
  /// Offset of the previous frame.
  uint64_t prev_off;
  /// Size of the data within the frame.
  uint64_t data_size;
} a0_transport_frame_hdr_t;

typedef struct a0_transport_frame_s {
  /// Frame header.
  a0_transport_frame_hdr_t hdr;
  /// Frame data.
  uint8_t* data;
} a0_transport_frame_t;

/// Whether the a0_transport_init call created or connected to the
/// transport in the given arena.
typedef enum a0_transport_init_status_s {
  A0_TRANSPORT_CREATED,
  A0_TRANSPORT_CONNECTED,
} a0_transport_init_status_t;

/// Wrapper around a transport, used to "strongly" type unique-access.
typedef struct a0_locked_transport_s {
  /// Wrapped transport.
  a0_transport_t* transport;
} a0_locked_transport_t;

/// Creates or connects to the transport in the given arena.
///
/// The transport is locked at end of init and MUST be released
/// by the caller once initialization is satisfied.
errno_t a0_transport_init(a0_transport_t*,
                          a0_arena_t,
                          a0_transport_init_status_t* status_out,
                          a0_locked_transport_t* lk_out);

/// Allocates space in the arena for metadata.
/// This can only be called after a0_transport_init, before any allocations.
/// Once the transports start allocating frames, the metadata size is fixed.
errno_t a0_transport_init_metadata(a0_locked_transport_t, size_t metadata_size);

/// Closes the transport.
errno_t a0_transport_close(a0_transport_t*);

/// Locks the transport.
errno_t a0_transport_lock(a0_transport_t*, a0_locked_transport_t* lk_out);
/// Unlocks the transport.
///
/// The locked_transport object is invalid afterwards.
errno_t a0_transport_unlock(a0_locked_transport_t);

/// Accesses the metadata space within the arena.
///
/// Caller does NOT own `metadata_out->ptr` and should not clean it up!
errno_t a0_transport_metadata(a0_locked_transport_t, a0_buf_t* metadata_out);

/// Checks whether the transport is empty.
errno_t a0_transport_empty(a0_locked_transport_t, bool*);
/// Checks whether the transport is not empty.
errno_t a0_transport_nonempty(a0_locked_transport_t, bool*);
/// Checks whether the user's transport pointer is valid.
errno_t a0_transport_ptr_valid(a0_locked_transport_t, bool*);
/// Moves the user's transport pointer to the oldest frame.
///
/// Note that this is inclusive.
errno_t a0_transport_jump_head(a0_locked_transport_t);
/// Moves the user's transport pointer to the newest frame.
///
/// Note that this is inclusive.
errno_t a0_transport_jump_tail(a0_locked_transport_t);
/// Checks whether a newer frame exists than that at the current
/// user's transport pointer.
errno_t a0_transport_has_next(a0_locked_transport_t, bool*);
/// Step the user's transport pointer forward by one frame.
///
/// Note: This steps to the oldest frame, still available, that was added
/// after the current frame. If the sequentially next frame has already
/// been evicted, this will effectively jump to head.
errno_t a0_transport_next(a0_locked_transport_t);
/// Checks whether a earlier frame exists than that at the current
/// user's transport pointer.
errno_t a0_transport_has_prev(a0_locked_transport_t, bool*);
/// Step the user's transport pointer backward by one frame.
errno_t a0_transport_prev(a0_locked_transport_t);

/// Await until the given predicate is satisfied.
///
/// The predicate is checked when an unlock event occurs following a commit or eviction.
///
/// TODO(lshamis): should pred take user_data?
errno_t a0_transport_await(a0_locked_transport_t, errno_t (*pred)(a0_locked_transport_t, bool*));

/// Accesses the frame within the arena, at the current transport pointer.
///
/// Caller does NOT own `frame_out->data` and should not clean it up!
errno_t a0_transport_frame(a0_locked_transport_t, a0_transport_frame_t* frame_out);

/// Allocates a new frame within the arena.
///
/// Caller does NOT own `frame_out->data` and should not clean it up!
///
/// For robustness, allocated frames are not tracked until explicitly commited.
///
/// \rst
/// .. note::
///
///     If an alloc evicts an old frame, that frame is lost, even if no
///     commit call is issued.
/// \endrst
errno_t a0_transport_alloc(a0_locked_transport_t, size_t, a0_transport_frame_t* frame_out);
/// Checks whether an alloc call would evict.
errno_t a0_transport_alloc_evicts(a0_locked_transport_t, size_t, bool*);
errno_t a0_transport_allocator(a0_locked_transport_t*, a0_alloc_t*);
errno_t a0_transport_commit(a0_locked_transport_t);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_TRANSPORT_H
