#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/time.h>
#include <a0/transport.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "err_macro.h"

typedef struct a0_transport_state_s {
  uint64_t seq_low;
  uint64_t seq_high;
  size_t off_head;
  size_t off_tail;
  size_t high_water_mark;
} a0_transport_state_t;

typedef struct a0_transport_version_s {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
} a0_transport_version_t;

typedef struct a0_transport_hdr_s {
  char magic[9]; /* ALEPHZERO */
  a0_transport_version_t version;
  bool initialized;

  a0_mtx_t mtx;
  a0_cnd_t cnd;

  a0_transport_state_t state_pages[2];
  uint8_t committed_page_idx;

  size_t arena_size;
} a0_transport_hdr_t;

// TODO(lshamis): Consider packing or reordering fields to reduce this.
_Static_assert(sizeof(a0_transport_hdr_t) == 144, "Unexpected transport binary representation.");

A0_STATIC_INLINE
a0_transport_hdr_t* a0_transport_header(a0_transport_locked_t lk) {
  return (a0_transport_hdr_t*)lk.transport->_arena.buf.data;
}

A0_STATIC_INLINE
a0_transport_frame_hdr_t* a0_transport_frame_header(a0_transport_locked_t lk, size_t off) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  return (a0_transport_frame_hdr_t*)((uint8_t*)hdr + off);
}

A0_STATIC_INLINE
a0_transport_state_t* a0_transport_committed_page(a0_transport_locked_t lk) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  return &hdr->state_pages[hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_transport_state_t* a0_transport_working_page(a0_transport_locked_t lk) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  return &hdr->state_pages[!hdr->committed_page_idx];
}

A0_STATIC_INLINE
size_t a0_max_align(size_t off) {
  return ((off + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

A0_STATIC_INLINE
size_t a0_transport_workspace_off() {
  return a0_max_align(sizeof(a0_transport_hdr_t));
}

// Converts a 0.2 transport into a 0.3 transport.
// Note: This does not allow 0.2 and 0.3 to run simultaniously.
//       0.2 transport will no longer work after this.
static void a0_backward_compatiblility_update_from_0_2(a0_arena_t)
    __attribute__((no_sanitize("thread")));

static void a0_backward_compatiblility_update_from_0_2(a0_arena_t arena) {
  if (*(uint16_t*)arena.buf.data != 0x0101) {
    // Not 0.2 format.
    return;
  }

  uint32_t committed_page_idx = *(uint32_t*)(&arena.buf.data[120]);

  uint32_t page_offset = committed_page_idx ? 88 : 56;
  uint8_t* ptr = &arena.buf.data[page_offset];

  uint64_t seq_low = *(uint64_t*)ptr;
  ptr += sizeof(uint64_t);

  uint64_t seq_high = *(uint64_t*)ptr;
  ptr += sizeof(uint64_t);

  uintptr_t off_head = *(uintptr_t*)ptr;
  ptr += sizeof(uintptr_t);

  uintptr_t off_tail = *(uintptr_t*)ptr;
  // ptr += sizeof(uintptr_t);

  a0_transport_hdr_t* hdr = (a0_transport_hdr_t*)arena.buf.data;
  memset(hdr, 0, sizeof(a0_transport_hdr_t));

  memcpy(hdr->magic, "ALEPHZERO", 9);
  hdr->version.major = 0;
  hdr->version.minor = 3;
  hdr->version.patch = 0;

  hdr->arena_size = arena.buf.size;
  hdr->state_pages[0].seq_low = seq_low;
  hdr->state_pages[0].seq_high = seq_high;
  hdr->state_pages[0].off_head = off_head;
  hdr->state_pages[0].off_tail = off_tail;
  hdr->state_pages[0].high_water_mark = hdr->arena_size;

  hdr->state_pages[1] = hdr->state_pages[0];
  hdr->initialized = true;
}

a0_err_t a0_transport_init(a0_transport_t* transport, a0_arena_t arena) {
  a0_backward_compatiblility_update_from_0_2(arena);
  // The arena is expected to be either:
  // 1) all null bytes.
  //    this is guaranteed by ftruncate, as is used in a0/file.h
  // 2) a pre-initialized buffer.
  a0_transport_hdr_t* hdr = (a0_transport_hdr_t*)arena.buf.data;

  memset(transport, 0, sizeof(a0_transport_t));
  transport->_arena = arena;

  if (transport->_arena.mode == A0_ARENA_MODE_EXCLUSIVE) {
    memset(&hdr->mtx, 0, sizeof(hdr->mtx));
  }

  a0_transport_locked_t lk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(transport, &lk));

  if (!hdr->initialized) {
    memcpy(hdr->magic, "ALEPHZERO", 9);
    hdr->version.major = 0;
    hdr->version.minor = 3;
    hdr->version.patch = 0;
    hdr->arena_size = transport->_arena.buf.size;
    hdr->state_pages[0].high_water_mark = a0_transport_workspace_off();
    hdr->state_pages[1].high_water_mark = a0_transport_workspace_off();
    hdr->initialized = true;
  } else {
    // TODO(lshamis): Verify magic + version.
  }

  a0_transport_unlock(lk);

  return A0_OK;
}

a0_err_t a0_transport_shutdown(a0_transport_locked_t lk) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);

  lk.transport->_shutdown = true;
  a0_cnd_broadcast(&hdr->cnd, &hdr->mtx);

  while (lk.transport->_wait_cnt) {
    a0_cnd_wait(&hdr->cnd, &hdr->mtx);
  }
  return A0_OK;
}

a0_err_t a0_transport_shutdown_requested(a0_transport_locked_t lk, bool* out) {
  *out = lk.transport->_shutdown;
  return A0_OK;
}

a0_err_t a0_transport_lock(a0_transport_t* transport, a0_transport_locked_t* lk_out) {
  lk_out->transport = transport;

  if (transport->_arena.mode != A0_ARENA_MODE_SHARED) {
    return A0_OK;
  }

  a0_transport_hdr_t* hdr = a0_transport_header(*lk_out);

  a0_err_t lock_status = a0_mtx_lock(&hdr->mtx);
  if (A0_SYSERR(lock_status) == EOWNERDEAD) {
    // The data is always consistent by design.
    lock_status = a0_mtx_consistent(&hdr->mtx);
  }

  // Clear any incomplete changes.
  *a0_transport_working_page(*lk_out) = *a0_transport_committed_page(*lk_out);

  return lock_status;
}

a0_err_t a0_transport_unlock(a0_transport_locked_t lk) {
  if (lk.transport->_arena.mode != A0_ARENA_MODE_SHARED) {
    return A0_OK;
  }

  *a0_transport_working_page(lk) = *a0_transport_committed_page(lk);
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  a0_mtx_unlock(&hdr->mtx);
  return A0_OK;
}

a0_err_t a0_transport_empty(a0_transport_locked_t lk, bool* out) {
  a0_transport_state_t* working_page = a0_transport_working_page(lk);
  *out = !working_page->seq_high | (working_page->seq_low > working_page->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_nonempty(a0_transport_locked_t lk, bool* out) {
  a0_err_t err = a0_transport_empty(lk, out);
  *out = !*out;
  return err;
}

a0_err_t a0_transport_iter_valid(a0_transport_locked_t lk, bool* out) {
  a0_transport_state_t* working_page = a0_transport_working_page(lk);
  *out = (working_page->seq_low <= lk.transport->_seq) &&
         (lk.transport->_seq <= working_page->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_jump(a0_transport_locked_t lk, size_t off) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  if (off + sizeof(a0_transport_frame_hdr_t) >= hdr->arena_size) {
    return A0_ERR_RANGE;
  }
  a0_transport_frame_hdr_t* frame_hdr = a0_transport_frame_header(lk, off);
  if (off + sizeof(a0_transport_frame_hdr_t) + frame_hdr->data_size >= hdr->arena_size) {
    return A0_ERR_RANGE;
  }

  lk.transport->_off = off;
  lk.transport->_seq = frame_hdr->seq;
  return A0_OK;
}

a0_err_t a0_transport_jump_head(a0_transport_locked_t lk) {
  a0_transport_state_t* state = a0_transport_working_page(lk);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(lk, &empty));
  if (empty) {
    return A0_ERR_RANGE;
  }

  lk.transport->_seq = state->seq_low;
  lk.transport->_off = state->off_head;
  return A0_OK;
}

a0_err_t a0_transport_jump_tail(a0_transport_locked_t lk) {
  a0_transport_state_t* state = a0_transport_working_page(lk);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(lk, &empty));
  if (empty) {
    return A0_ERR_RANGE;
  }

  lk.transport->_seq = state->seq_high;
  lk.transport->_off = state->off_tail;
  return A0_OK;
}

a0_err_t a0_transport_has_next(a0_transport_locked_t lk, bool* out) {
  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(lk, &empty));

  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = !empty && (lk.transport->_seq < state->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_step_next(a0_transport_locked_t lk) {
  a0_transport_state_t* state = a0_transport_working_page(lk);

  bool has_next;
  A0_RETURN_ERR_ON_ERR(a0_transport_has_next(lk, &has_next));
  if (!has_next) {
    return A0_ERR_RANGE;
  }

  if (lk.transport->_seq < state->seq_low) {
    lk.transport->_seq = state->seq_low;
    lk.transport->_off = state->off_head;
    return A0_OK;
  }

  lk.transport->_off = a0_transport_frame_header(lk, lk.transport->_off)->next_off;
  lk.transport->_seq = a0_transport_frame_header(lk, lk.transport->_off)->seq;

  return A0_OK;
}

a0_err_t a0_transport_has_prev(a0_transport_locked_t lk, bool* out) {
  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(lk, &empty));

  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = !empty && (lk.transport->_seq > state->seq_low);
  return A0_OK;
}

a0_err_t a0_transport_step_prev(a0_transport_locked_t lk) {
  bool has_prev;
  A0_RETURN_ERR_ON_ERR(a0_transport_has_prev(lk, &has_prev));
  if (!has_prev) {
    return A0_ERR_RANGE;
  }

  lk.transport->_off = a0_transport_frame_header(lk, lk.transport->_off)->prev_off;
  lk.transport->_seq = a0_transport_frame_header(lk, lk.transport->_off)->seq;

  return A0_OK;
}

typedef struct a0_transport_timedwait_data_s {
  a0_predicate_t user_pred;
  a0_time_mono_t timeout;
} a0_transport_timedwait_data_t;

A0_STATIC_INLINE
a0_err_t a0_transport_timedwait_predicate(void* data_, bool* out) {
  a0_transport_timedwait_data_t* data = (a0_transport_timedwait_data_t*)data_;

  a0_time_mono_t now;
  a0_time_mono_now(&now);

  uint64_t now_ns = now.ts.tv_sec * NS_PER_SEC + now.ts.tv_nsec;
  uint64_t timeout_ns = data->timeout.ts.tv_sec * NS_PER_SEC + data->timeout.ts.tv_nsec;

  if (now_ns >= timeout_ns) {
    return A0_MAKE_SYSERR(ETIMEDOUT);
  }
  return a0_predicate_eval(data->user_pred, out);
}

A0_STATIC_INLINE
a0_err_t a0_transport_timedwait_impl(a0_transport_locked_t lk, a0_predicate_t pred, const a0_time_mono_t* timeout) {
  if (lk.transport->_shutdown) {
    return A0_MAKE_SYSERR(ESHUTDOWN);
  }
  if (lk.transport->_arena.mode != A0_ARENA_MODE_SHARED) {
    return A0_MAKE_SYSERR(EPERM);
  }
  a0_transport_hdr_t* hdr = a0_transport_header(lk);

  bool sat = false;
  a0_err_t err = a0_predicate_eval(pred, &sat);
  if (err | sat) {
    return err;
  }

  lk.transport->_wait_cnt++;

  while (!lk.transport->_shutdown) {
    if (timeout) {
      err = a0_cnd_timedwait(&hdr->cnd, &hdr->mtx, *timeout);
      if (A0_SYSERR(err) == ETIMEDOUT) {
        break;
      }
    } else {
      a0_cnd_wait(&hdr->cnd, &hdr->mtx);
    }

    err = a0_predicate_eval(pred, &sat);
    if (err | sat) {
      break;
    }
  }
  if (!err && lk.transport->_shutdown) {
    err = A0_MAKE_SYSERR(ESHUTDOWN);
  }

  lk.transport->_wait_cnt--;
  a0_cnd_broadcast(&hdr->cnd, &hdr->mtx);

  return err;
}

a0_err_t a0_transport_timedwait(a0_transport_locked_t lk, a0_predicate_t pred, a0_time_mono_t timeout) {
  a0_transport_timedwait_data_t data = (a0_transport_timedwait_data_t){
      .user_pred = pred,
      .timeout = timeout,
  };
  a0_predicate_t full_pred = (a0_predicate_t){
      .user_data = &data,
      .fn = a0_transport_timedwait_predicate,
  };

  return a0_transport_timedwait_impl(lk, full_pred, &timeout);
}

a0_err_t a0_transport_wait(a0_transport_locked_t lk, a0_predicate_t pred) {
  return a0_transport_timedwait_impl(lk, pred, NULL);
}

A0_STATIC_INLINE
a0_err_t a0_transport_empty_pred_fn(void* user_data, bool* out) {
  return a0_transport_empty(*(a0_transport_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_empty_pred(a0_transport_locked_t* lk) {
  return (a0_predicate_t){
      .user_data = lk,
      .fn = a0_transport_empty_pred_fn,
  };
}

A0_STATIC_INLINE
a0_err_t a0_transport_nonempty_pred_fn(void* user_data, bool* out) {
  return a0_transport_nonempty(*(a0_transport_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_nonempty_pred(a0_transport_locked_t* lk) {
  return (a0_predicate_t){
      .user_data = lk,
      .fn = a0_transport_nonempty_pred_fn,
  };
}

A0_STATIC_INLINE
a0_err_t a0_transport_has_next_pred_fn(void* user_data, bool* out) {
  return a0_transport_has_next(*(a0_transport_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_has_next_pred(a0_transport_locked_t* lk) {
  return (a0_predicate_t){
      .user_data = lk,
      .fn = a0_transport_has_next_pred_fn,
  };
}

a0_err_t a0_transport_seq_low(a0_transport_locked_t lk, uint64_t* out) {
  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = state->seq_low;
  return A0_OK;
}

a0_err_t a0_transport_seq_high(a0_transport_locked_t lk, uint64_t* out) {
  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = state->seq_high;
  return A0_OK;
}

a0_err_t a0_transport_frame(a0_transport_locked_t lk, a0_transport_frame_t* frame_out) {
  a0_transport_state_t* state = a0_transport_working_page(lk);

  if (lk.transport->_seq < state->seq_low) {
    return A0_MAKE_SYSERR(ESPIPE);
  }

  a0_transport_frame_hdr_t* frame_hdr = a0_transport_frame_header(lk, lk.transport->_off);

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)frame_hdr + sizeof(a0_transport_frame_hdr_t);
  return A0_OK;
}

A0_STATIC_INLINE
size_t a0_transport_frame_end(a0_transport_locked_t lk, size_t frame_off) {
  a0_transport_frame_hdr_t* frame_hdr = a0_transport_frame_header(lk, frame_off);
  return frame_off + sizeof(a0_transport_frame_hdr_t) + frame_hdr->data_size;
}

A0_STATIC_INLINE
bool a0_transport_frame_intersects(size_t frame1_start,
                                   size_t frame1_size,
                                   size_t frame2_start,
                                   size_t frame2_size) {
  size_t frame1_end = frame1_start + frame1_size;
  size_t frame2_end = frame2_start + frame2_size;
  return (frame1_start < frame2_end) && (frame2_start < frame1_end);
}

A0_STATIC_INLINE
bool a0_transport_head_interval(a0_transport_locked_t lk,
                                a0_transport_state_t* state,
                                size_t* head_off,
                                size_t* head_size) {
  bool empty;
  a0_transport_empty(lk, &empty);
  if (empty) {
    return false;
  }

  *head_off = state->off_head;
  a0_transport_frame_hdr_t* head_hdr = a0_transport_frame_header(lk, *head_off);
  *head_size = sizeof(a0_transport_frame_hdr_t) + head_hdr->data_size;
  return true;
}

A0_STATIC_INLINE
void a0_transport_remove_head(a0_transport_locked_t lk, a0_transport_state_t* state) {
  if (state->off_head == state->off_tail) {
    state->off_head = 0;
    state->off_tail = 0;
    state->high_water_mark = a0_transport_workspace_off();
  } else {
    a0_transport_frame_hdr_t* head_hdr = a0_transport_frame_header(lk, state->off_head);
    state->off_head = head_hdr->next_off;

    // Check whether the old head frame was responsible for the high water mark.
    size_t head_end = a0_transport_frame_end(lk, head_hdr->off);
    if (state->high_water_mark == head_end) {
      // The high water mark is always set by a tail element.
      state->high_water_mark = a0_transport_frame_end(lk, state->off_tail);
    }
  }
  state->seq_low++;

  a0_transport_commit(lk);
}

A0_STATIC_INLINE
a0_err_t a0_transport_find_slot(a0_transport_locked_t lk, size_t frame_size, size_t* off) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  a0_transport_state_t* state = a0_transport_working_page(lk);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(lk, &empty));

  if (empty) {
    *off = a0_transport_workspace_off();
  } else {
    *off = a0_max_align(a0_transport_frame_end(lk, state->off_tail));
    if (*off + frame_size >= hdr->arena_size) {
      *off = a0_transport_workspace_off();
    }
  }

  if (*off + frame_size > hdr->arena_size) {
    return A0_ERR_FRAME_LARGE;
  }

  return A0_OK;
}

A0_STATIC_INLINE
void a0_transport_evict(a0_transport_locked_t lk, size_t off, size_t frame_size) {
  size_t head_off;
  size_t head_size;
  a0_transport_state_t* state = a0_transport_working_page(lk);
  while (a0_transport_head_interval(lk, state, &head_off, &head_size) &&
         a0_transport_frame_intersects(off, frame_size, head_off, head_size)) {
    a0_transport_remove_head(lk, state);
  }
}

A0_STATIC_INLINE
void a0_transport_slot_init(a0_transport_state_t* state,
                            a0_transport_frame_hdr_t* frame_hdr,
                            size_t off,
                            size_t size) {
  memset(frame_hdr, 0, sizeof(a0_transport_frame_hdr_t));

  frame_hdr->seq = ++state->seq_high;
  if (!state->seq_low) {
    state->seq_low = frame_hdr->seq;
  }

  frame_hdr->off = off;
  frame_hdr->next_off = 0;

  frame_hdr->data_size = size;
}

A0_STATIC_INLINE
void a0_transport_maybe_set_head(a0_transport_state_t* state, a0_transport_frame_hdr_t* frame_hdr) {
  if (!state->off_head) {
    state->off_head = frame_hdr->off;
  }
}

A0_STATIC_INLINE
void a0_transport_update_tail(a0_transport_locked_t lk,
                              a0_transport_state_t* state,
                              a0_transport_frame_hdr_t* frame_hdr) {
  if (state->off_tail) {
    a0_transport_frame_hdr_t* tail_frame_hdr = a0_transport_frame_header(lk, state->off_tail);
    tail_frame_hdr->next_off = frame_hdr->off;
    frame_hdr->prev_off = state->off_tail;
  }
  state->off_tail = frame_hdr->off;
}

A0_STATIC_INLINE
void a0_transport_update_high_water_mark(a0_transport_locked_t lk,
                                         a0_transport_state_t* state,
                                         a0_transport_frame_hdr_t* frame_hdr) {
  size_t high_water_mark = a0_transport_frame_end(lk, frame_hdr->off);
  if (state->high_water_mark < high_water_mark) {
    state->high_water_mark = high_water_mark;
  }
}

a0_err_t a0_transport_alloc_evicts(a0_transport_locked_t lk, size_t size, bool* out) {
  size_t frame_size = sizeof(a0_transport_frame_hdr_t) + size;

  size_t off;
  A0_RETURN_ERR_ON_ERR(a0_transport_find_slot(lk, frame_size, &off));

  size_t head_off;
  size_t head_size;
  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = a0_transport_head_interval(lk, state, &head_off, &head_size) &&
         a0_transport_frame_intersects(off, frame_size, head_off, head_size);

  return A0_OK;
}

a0_err_t a0_transport_alloc(a0_transport_locked_t lk, size_t size, a0_transport_frame_t* frame_out) {
  if (lk.transport->_arena.mode == A0_ARENA_MODE_READONLY) {
    return A0_MAKE_SYSERR(EPERM);
  }
  size_t frame_size = sizeof(a0_transport_frame_hdr_t) + size;

  size_t off;
  A0_RETURN_ERR_ON_ERR(a0_transport_find_slot(lk, frame_size, &off));

  a0_transport_evict(lk, off, frame_size);

  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  // Note: a0_transport_evict commits changes, which invalidates state.
  //       Must grab state afterwards.
  a0_transport_state_t* state = a0_transport_working_page(lk);

  a0_transport_frame_hdr_t* frame_hdr = a0_transport_frame_header(lk, off);
  a0_transport_slot_init(state, frame_hdr, off, size);

  a0_transport_maybe_set_head(state, frame_hdr);
  a0_transport_update_tail(lk, state, frame_hdr);
  a0_transport_update_high_water_mark(lk, state, frame_hdr);

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)hdr + off + sizeof(a0_transport_frame_hdr_t);

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_transport_allocator_impl(void* user_data, size_t size, a0_buf_t* buf_out) {
  a0_transport_frame_t frame;
  A0_RETURN_ERR_ON_ERR(a0_transport_alloc(*(a0_transport_locked_t*)user_data, size, &frame));
  *buf_out = (a0_buf_t){frame.data, frame.hdr.data_size};
  return A0_OK;
}

a0_err_t a0_transport_allocator(a0_transport_locked_t* lk, a0_alloc_t* alloc_out) {
  alloc_out->user_data = lk;
  alloc_out->alloc = a0_transport_allocator_impl;
  alloc_out->dealloc = NULL;
  return A0_OK;
}

a0_err_t a0_transport_commit(a0_transport_locked_t lk) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  // Assume page A was the previously committed page and page B is the working
  // page that is ready to be committed. Both represent a valid state for the
  // transport. It's possible that the copying of B into A will fail (prog crash),
  // leaving A in an inconsistent state. We set B as the committed page, before
  // copying the page info.
  hdr->committed_page_idx = !hdr->committed_page_idx;
  *a0_transport_working_page(lk) = *a0_transport_committed_page(lk);

  a0_cnd_broadcast(&hdr->cnd, &hdr->mtx);

  return A0_OK;
}

a0_err_t a0_transport_used_space(a0_transport_locked_t lk, size_t* out) {
  a0_transport_state_t* state = a0_transport_working_page(lk);
  *out = state->high_water_mark;
  return A0_OK;
}

a0_err_t a0_transport_resize(a0_transport_locked_t lk, size_t arena_size) {
  size_t used_space;
  A0_RETURN_ERR_ON_ERR(a0_transport_used_space(lk, &used_space));
  if (arena_size < used_space) {
    return A0_ERR_INVALID_ARG;
  }

  a0_transport_hdr_t* hdr = a0_transport_header(lk);
  hdr->arena_size = arena_size;
  return A0_OK;
}

A0_STATIC_INLINE
void write_limited(FILE* f, a0_buf_t str) {
  size_t line_size = str.size;
  bool overflow = false;
  if (line_size > 32) {
    line_size = 29;
    overflow = true;
  }
  fwrite(str.data, sizeof(char), line_size, f);
  if (overflow) {
    fprintf(f, "...");
  }
}

void a0_transport_debugstr(a0_transport_locked_t lk, a0_buf_t* out) {
  a0_transport_hdr_t* hdr = a0_transport_header(lk);

  a0_transport_state_t* committed_state = a0_transport_committed_page(lk);
  a0_transport_state_t* working_state = a0_transport_working_page(lk);

  FILE* ss = open_memstream((char**)&out->data, &out->size);
  // clang-format off
  fprintf(ss, "\n{\n");
  fprintf(ss, "  \"header\": {\n");
  fprintf(ss, "    \"arena_size\": %lu,\n", hdr->arena_size);
  fprintf(ss, "    \"committed_state\": {\n");
  fprintf(ss, "      \"seq_low\": %lu,\n", committed_state->seq_low);
  fprintf(ss, "      \"seq_high\": %lu,\n", committed_state->seq_high);
  fprintf(ss, "      \"off_head\": %lu,\n", committed_state->off_head);
  fprintf(ss, "      \"off_tail\": %lu,\n", committed_state->off_tail);
  fprintf(ss, "      \"high_water_mark\": %lu\n", committed_state->high_water_mark);
  fprintf(ss, "    },\n");
  fprintf(ss, "    \"working_state\": {\n");
  fprintf(ss, "      \"seq_low\": %lu,\n", working_state->seq_low);
  fprintf(ss, "      \"seq_high\": %lu,\n", working_state->seq_high);
  fprintf(ss, "      \"off_head\": %lu,\n", working_state->off_head);
  fprintf(ss, "      \"off_tail\": %lu,\n", working_state->off_tail);
  fprintf(ss, "      \"high_water_mark\": %lu\n", working_state->high_water_mark);
  fprintf(ss, "    }\n");
  fprintf(ss, "  },\n");
  fprintf(ss, "  \"data\": [\n");
  // clang-format on

  if (working_state->off_head) {
    size_t off = working_state->off_head;
    bool first = true;
    while (true) {
      a0_transport_frame_hdr_t* frame_hdr = a0_transport_frame_header(lk, off);
      uint64_t seq = frame_hdr->seq;

      if (!first) {
        fprintf(ss, "    },\n");
      }
      first = false;

      fprintf(ss, "    {\n");
      if (seq > committed_state->seq_high) {
        fprintf(ss, "      \"committed\": false,\n");
      }
      fprintf(ss, "      \"off\": %lu,\n", frame_hdr->off);
      fprintf(ss, "      \"seq\": %lu,\n", frame_hdr->seq);
      fprintf(ss, "      \"prev_off\": %lu,\n", frame_hdr->prev_off);
      fprintf(ss, "      \"next_off\": %lu,\n", frame_hdr->next_off);
      fprintf(ss, "      \"data_size\": %lu,\n", frame_hdr->data_size);
      a0_buf_t data = {
          .data = (uint8_t*)hdr + frame_hdr->off + sizeof(a0_transport_frame_hdr_t),
          .size = frame_hdr->data_size,
      };
      fprintf(ss, "      \"data\": \"");
      write_limited(ss, data);
      fprintf(ss, "\"\n");

      off = frame_hdr->next_off;

      if (seq == working_state->seq_high) {
        fprintf(ss, "    }\n");
        break;
      }
    }
  }
  fprintf(ss, "  ]\n");
  fprintf(ss, "}\n");
  fflush(ss);
  fclose(ss);
}
