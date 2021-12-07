#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/rwmtx.h>
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

#define A0_NUM_RMTX_SLOT 16

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

  a0_mtx_t init_mtx;

  a0_rwmtx_t rwmtx;
  a0_mtx_t rwmtx_rslots[A0_NUM_RMTX_SLOT];
  a0_rwcnd_t rwcnd;

  a0_transport_state_t state_pages[2];
  uint8_t committed_page_idx;

  size_t arena_size;
} a0_transport_hdr_t;

_Static_assert(sizeof(a0_transport_hdr_t) == 616, "Unexpected transport binary representation.");

A0_STATIC_INLINE
a0_arena_t twl_arena(a0_transport_writer_locked_t* twl) {
  return twl->_tw->_arena;
}

A0_STATIC_INLINE
a0_arena_t trl_arena(a0_transport_reader_locked_t* trl) {
  return trl->_tr->_arena;
}

A0_STATIC_INLINE
a0_transport_hdr_t* twl_header(a0_transport_writer_locked_t* twl) {
  return (a0_transport_hdr_t*)twl_arena(twl).buf.data;
}

A0_STATIC_INLINE
a0_transport_hdr_t* trl_header(a0_transport_reader_locked_t* trl) {
  return (a0_transport_hdr_t*)trl_arena(trl).buf.data;
}

A0_STATIC_INLINE
a0_transport_frame_hdr_t* twl_frame_header(a0_transport_writer_locked_t* twl, size_t off) {
  a0_transport_hdr_t* hdr = twl_header(twl);
  return (a0_transport_frame_hdr_t*)((uint8_t*)hdr + off);
}

A0_STATIC_INLINE
a0_transport_frame_hdr_t* trl_frame_header(a0_transport_reader_locked_t* trl, size_t off) {
  a0_transport_hdr_t* hdr = trl_header(trl);
  return (a0_transport_frame_hdr_t*)((uint8_t*)hdr + off);
}

A0_STATIC_INLINE
a0_transport_state_t* trl_committed_page(a0_transport_reader_locked_t* trl) {
  a0_transport_hdr_t* hdr = trl_header(trl);
  return &hdr->state_pages[hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_transport_state_t* twl_committed_page(a0_transport_writer_locked_t* twl) {
  a0_transport_hdr_t* hdr = twl_header(twl);
  return &hdr->state_pages[hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_transport_state_t* twl_working_page(a0_transport_writer_locked_t* twl) {
  a0_transport_hdr_t* hdr = twl_header(twl);
  return &hdr->state_pages[!hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_rwmtx_rmtx_span_t trl_rwmtx_span(a0_transport_reader_locked_t* trl) {
  return (a0_rwmtx_rmtx_span_t){trl_header(trl)->rwmtx_rslots, A0_NUM_RMTX_SLOT};
}

A0_STATIC_INLINE
a0_rwmtx_rmtx_span_t twl_rwmtx_span(a0_transport_writer_locked_t* twl) {
  return (a0_rwmtx_rmtx_span_t){twl_header(twl)->rwmtx_rslots, A0_NUM_RMTX_SLOT};
}

A0_STATIC_INLINE
size_t max_align(size_t off) {
  return ((off + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

A0_STATIC_INLINE
size_t workspace_off() {
  return max_align(sizeof(a0_transport_hdr_t));
}

A0_STATIC_INLINE
a0_err_t transport_init(a0_arena_t arena) {
  // The arena is expected to be either:
  // 1) all null bytes.
  //    this is guaranteed by ftruncate, as is used in a0/file.h
  // 2) a pre-initialized buffer.
  a0_transport_hdr_t* hdr = (a0_transport_hdr_t*)arena.buf.data;

  if (arena.mode == A0_ARENA_MODE_EXCLUSIVE) {
    memset(&hdr->rwmtx, 0, sizeof(hdr->rwmtx));
    memset(&hdr->rwmtx_rslots, 0, sizeof(hdr->rwmtx_rslots));
  }

  if (A0_SYSERR(a0_mtx_lock(&hdr->init_mtx)) == EOWNERDEAD) {
    a0_mtx_consistent(&hdr->init_mtx);
  }

  if (!hdr->initialized) {
    memcpy(hdr->magic, "ALEPHZERO", 9);
    hdr->version.major = 0;
    hdr->version.minor = 4;
    hdr->version.patch = 0;
    hdr->arena_size = arena.buf.size;
    hdr->state_pages[0].high_water_mark = workspace_off();
    hdr->state_pages[1].high_water_mark = workspace_off();
    hdr->initialized = true;
  } else {
    // TODO(lshamis): Verify magic + version.
  }

  a0_mtx_unlock(&hdr->init_mtx);

  return A0_OK;
}

a0_err_t a0_transport_reader_init(a0_transport_reader_t* tr, a0_arena_t arena) {
  memset(tr, 0, sizeof(a0_transport_reader_t));
  A0_RETURN_ERR_ON_ERR(transport_init(arena));
  tr->_arena = arena;
  return A0_OK;
}

a0_err_t a0_transport_writer_init(a0_transport_writer_t* tw, a0_arena_t arena) {
  memset(tw, 0, sizeof(a0_transport_writer_t));
  A0_RETURN_ERR_ON_ERR(transport_init(arena));
  tw->_arena = arena;
  return A0_OK;
}

a0_err_t a0_transport_reader_shutdown(a0_transport_reader_locked_t* trl) {
  a0_transport_hdr_t* hdr = trl_header(trl);

  trl->_tr->_shutdown = true;
  a0_rwcnd_broadcast(&hdr->rwcnd);

  while (trl->_tr->_wait_cnt) {
    a0_rwcnd_wait(&hdr->rwcnd, &hdr->rwmtx, trl_rwmtx_span(trl), &trl->_tkn);
  }
  return A0_OK;
}

a0_err_t a0_transport_reader_lock(a0_transport_reader_t* tr, a0_transport_reader_locked_t* trl_out) {
  trl_out->_tr = tr;
  if (tr->_arena.mode == A0_ARENA_MODE_SHARED) {
    a0_rwmtx_rlock(&trl_header(trl_out)->rwmtx, trl_rwmtx_span(trl_out), &trl_out->_tkn);
  }
  return A0_OK;
}

a0_err_t a0_transport_writer_lock(a0_transport_writer_t* tw, a0_transport_writer_locked_t* twl_out) {
  twl_out->_tw = tw;
  if (tw->_arena.mode == A0_ARENA_MODE_SHARED) {
    a0_rwmtx_wlock(&twl_header(twl_out)->rwmtx, twl_rwmtx_span(twl_out), &twl_out->_tkn);
    *twl_working_page(twl_out) = *twl_committed_page(twl_out);
  }
  return A0_OK;
}

a0_err_t a0_transport_reader_unlock(a0_transport_reader_locked_t* trl) {
  if (trl_arena(trl).mode == A0_ARENA_MODE_SHARED) {
    a0_rwmtx_unlock(&trl_header(trl)->rwmtx, trl->_tkn);
  }
  return A0_OK;
}

a0_err_t a0_transport_writer_unlock(a0_transport_writer_locked_t* twl) {
  *twl_working_page(twl) = *twl_committed_page(twl);
  if (twl_arena(twl).mode == A0_ARENA_MODE_SHARED) {
    a0_rwmtx_unlock(&twl_header(twl)->rwmtx, twl->_tkn);
  }
  return A0_OK;
}

a0_err_t a0_transport_writer_as_reader(a0_transport_writer_locked_t* twl, a0_transport_reader_t* tr_out, a0_transport_reader_locked_t* trl_out) {
  memset(tr_out, 0, sizeof(a0_transport_reader_t));
  memset(trl_out, 0, sizeof(a0_transport_reader_locked_t));
  tr_out->_arena = twl->_tw->_arena;
  trl_out->_tr = tr_out;
  return A0_OK;
}

a0_err_t a0_transport_writer_empty(a0_transport_writer_locked_t* twl, bool* out) {
  a0_transport_state_t* working_page = twl_working_page(twl);
  *out = !working_page->seq_high | (working_page->seq_low > working_page->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_reader_empty(a0_transport_reader_locked_t* trl, bool* out) {
  a0_transport_state_t* committed_page = trl_committed_page(trl);
  *out = !committed_page->seq_high | (committed_page->seq_low > committed_page->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_reader_nonempty(a0_transport_reader_locked_t* trl, bool* out) {
  a0_err_t err = a0_transport_reader_empty(trl, out);
  *out = !*out;
  return err;
}

a0_err_t a0_transport_reader_iter_valid(a0_transport_reader_locked_t* trl, bool* out) {
  a0_transport_state_t* committed_page = trl_committed_page(trl);
  *out = (committed_page->seq_low <= trl->_tr->_seq) &&
         (trl->_tr->_seq <= committed_page->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_reader_jump(a0_transport_reader_locked_t* trl, size_t off) {
  if (max_align(off) != off) {
    return A0_ERR_RANGE;
  }

  a0_transport_hdr_t* hdr = trl_header(trl);
  if (off + sizeof(a0_transport_frame_hdr_t) >= hdr->arena_size) {
    return A0_ERR_RANGE;
  }

  a0_transport_frame_hdr_t* frame_hdr = trl_frame_header(trl, off);
  if (off + sizeof(a0_transport_frame_hdr_t) + frame_hdr->data_size >= hdr->arena_size) {
    return A0_ERR_RANGE;
  }

  trl->_tr->_off = off;
  trl->_tr->_seq = frame_hdr->seq;
  return A0_OK;
}

a0_err_t a0_transport_reader_jump_head(a0_transport_reader_locked_t* trl) {
  a0_transport_state_t* state = trl_committed_page(trl);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_empty(trl, &empty));
  if (empty) {
    return A0_ERR_RANGE;
  }

  trl->_tr->_seq = state->seq_low;
  trl->_tr->_off = state->off_head;
  return A0_OK;
}

a0_err_t a0_transport_reader_jump_tail(a0_transport_reader_locked_t* trl) {
  a0_transport_state_t* state = trl_committed_page(trl);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_empty(trl, &empty));
  if (empty) {
    return A0_ERR_RANGE;
  }

  trl->_tr->_seq = state->seq_high;
  trl->_tr->_off = state->off_tail;
  return A0_OK;
}

a0_err_t a0_transport_reader_has_next(a0_transport_reader_locked_t* trl, bool* out) {
  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_empty(trl, &empty));

  a0_transport_state_t* state = trl_committed_page(trl);
  *out = !empty && (trl->_tr->_seq < state->seq_high);
  return A0_OK;
}

a0_err_t a0_transport_reader_step_next(a0_transport_reader_locked_t* trl) {
  a0_transport_state_t* state = trl_committed_page(trl);

  bool has_next;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_has_next(trl, &has_next));
  if (!has_next) {
    return A0_ERR_RANGE;
  }

  if (trl->_tr->_seq < state->seq_low) {
    trl->_tr->_seq = state->seq_low;
    trl->_tr->_off = state->off_head;
    return A0_OK;
  }

  trl->_tr->_off = trl_frame_header(trl, trl->_tr->_off)->next_off;
  trl->_tr->_seq = trl_frame_header(trl, trl->_tr->_off)->seq;

  return A0_OK;
}

a0_err_t a0_transport_reader_has_prev(a0_transport_reader_locked_t* trl, bool* out) {
  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_empty(trl, &empty));

  a0_transport_state_t* state = trl_committed_page(trl);
  *out = !empty && (trl->_tr->_seq > state->seq_low);
  return A0_OK;
}

a0_err_t a0_transport_reader_step_prev(a0_transport_reader_locked_t* trl) {
  bool has_prev;
  A0_RETURN_ERR_ON_ERR(a0_transport_reader_has_prev(trl, &has_prev));
  if (!has_prev) {
    return A0_ERR_RANGE;
  }

  trl->_tr->_off = trl_frame_header(trl, trl->_tr->_off)->prev_off;
  trl->_tr->_seq = trl_frame_header(trl, trl->_tr->_off)->seq;

  return A0_OK;
}

typedef struct a0_transport_timedwait_data_s {
  a0_predicate_t user_pred;
  a0_time_mono_t timeout;
} a0_transport_timedwait_data_t;

A0_STATIC_INLINE
a0_err_t a0_transport_reader_timedwait_predicate(void* data_, bool* out) {
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
a0_err_t timedwait_impl(a0_transport_reader_locked_t* trl, a0_predicate_t pred, const a0_time_mono_t* timeout) {
  if (trl->_tr->_shutdown) {
    return A0_MAKE_SYSERR(ESHUTDOWN);
  }
  if (trl->_tr->_arena.mode != A0_ARENA_MODE_SHARED) {
    return A0_MAKE_SYSERR(EPERM);
  }
  a0_transport_hdr_t* hdr = trl_header(trl);

  bool sat = false;
  a0_err_t err = a0_predicate_eval(pred, &sat);
  if (err | sat) {
    return err;
  }

  trl->_tr->_wait_cnt++;

  while (!trl->_tr->_shutdown) {
    if (timeout) {
      err = a0_rwcnd_timedwait(&hdr->rwcnd, &hdr->rwmtx, trl_rwmtx_span(trl), *timeout, &trl->_tkn);
      if (A0_SYSERR(err) == ETIMEDOUT) {
        break;
      }
    } else {
      a0_rwcnd_wait(&hdr->rwcnd, &hdr->rwmtx, trl_rwmtx_span(trl), &trl->_tkn);
    }

    err = a0_predicate_eval(pred, &sat);
    if (err | sat) {
      break;
    }
  }
  if (!err && trl->_tr->_shutdown) {
    err = A0_MAKE_SYSERR(ESHUTDOWN);
  }

  trl->_tr->_wait_cnt--;
  a0_rwcnd_broadcast(&hdr->rwcnd);

  return err;
}

a0_err_t a0_transport_reader_timedwait(a0_transport_reader_locked_t* trl, a0_predicate_t pred, a0_time_mono_t timeout) {
  a0_transport_timedwait_data_t data = (a0_transport_timedwait_data_t){
      .user_pred = pred,
      .timeout = timeout,
  };
  a0_predicate_t full_pred = (a0_predicate_t){
      .user_data = &data,
      .fn = a0_transport_reader_timedwait_predicate,
  };

  return timedwait_impl(trl, full_pred, &timeout);
}

a0_err_t a0_transport_reader_wait(a0_transport_reader_locked_t* trl, a0_predicate_t pred) {
  return timedwait_impl(trl, pred, NULL);
}

A0_STATIC_INLINE
a0_err_t empty_pred_fn(void* user_data, bool* out) {
  return a0_transport_reader_empty((a0_transport_reader_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_reader_empty_pred(a0_transport_reader_locked_t* trl) {
  return (a0_predicate_t){
      .user_data = trl,
      .fn = empty_pred_fn,
  };
}

A0_STATIC_INLINE
a0_err_t nonempty_pred_fn(void* user_data, bool* out) {
  return a0_transport_reader_nonempty((a0_transport_reader_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_reader_nonempty_pred(a0_transport_reader_locked_t* trl) {
  return (a0_predicate_t){
      .user_data = trl,
      .fn = nonempty_pred_fn,
  };
}

A0_STATIC_INLINE
a0_err_t has_next_pred_fn(void* user_data, bool* out) {
  return a0_transport_reader_has_next((a0_transport_reader_locked_t*)user_data, out);
}

a0_predicate_t a0_transport_reader_has_next_pred(a0_transport_reader_locked_t* trl) {
  return (a0_predicate_t){
      .user_data = trl,
      .fn = has_next_pred_fn,
  };
}

a0_err_t a0_transport_writer_seq_low(a0_transport_writer_locked_t* twl, uint64_t* out) {
  a0_transport_state_t* state = twl_working_page(twl);
  *out = state->seq_low;
  return A0_OK;
}

a0_err_t a0_transport_writer_seq_high(a0_transport_writer_locked_t* twl, uint64_t* out) {
  a0_transport_state_t* state = twl_working_page(twl);
  *out = state->seq_high;
  return A0_OK;
}

a0_err_t a0_transport_reader_seq_low(a0_transport_reader_locked_t* trl, uint64_t* out) {
  a0_transport_state_t* state = trl_committed_page(trl);
  *out = state->seq_low;
  return A0_OK;
}

a0_err_t a0_transport_reader_seq_high(a0_transport_reader_locked_t* trl, uint64_t* out) {
  a0_transport_state_t* state = trl_committed_page(trl);
  *out = state->seq_high;
  return A0_OK;
}

a0_err_t a0_transport_reader_frame(a0_transport_reader_locked_t* trl, a0_transport_frame_t* frame_out) {
  a0_transport_state_t* state = trl_committed_page(trl);

  if (trl->_tr->_seq < state->seq_low) {
    return A0_MAKE_SYSERR(ESPIPE);
  }

  a0_transport_frame_hdr_t* frame_hdr = trl_frame_header(trl, trl->_tr->_off);

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)frame_hdr + sizeof(a0_transport_frame_hdr_t);
  return A0_OK;
}

A0_STATIC_INLINE
size_t frame_end(a0_transport_writer_locked_t* twl, size_t frame_off) {
  a0_transport_frame_hdr_t* frame_hdr = twl_frame_header(twl, frame_off);
  return frame_off + sizeof(a0_transport_frame_hdr_t) + frame_hdr->data_size;
}

A0_STATIC_INLINE
bool frame_intersects(size_t frame1_start,
                      size_t frame1_size,
                      size_t frame2_start,
                      size_t frame2_size) {
  size_t frame1_end = frame1_start + frame1_size;
  size_t frame2_end = frame2_start + frame2_size;
  return (frame1_start < frame2_end) && (frame2_start < frame1_end);
}

A0_STATIC_INLINE
bool head_interval(a0_transport_writer_locked_t* twl,
                   a0_transport_state_t* state,
                   size_t* head_off,
                   size_t* head_size) {
  bool empty;
  a0_transport_writer_empty(twl, &empty);
  if (empty) {
    return false;
  }

  *head_off = state->off_head;
  a0_transport_frame_hdr_t* head_hdr = twl_frame_header(twl, *head_off);
  *head_size = sizeof(a0_transport_frame_hdr_t) + head_hdr->data_size;
  return true;
}

A0_STATIC_INLINE
void remove_head(a0_transport_writer_locked_t* twl, a0_transport_state_t* state) {
  if (state->off_head == state->off_tail) {
    state->off_head = 0;
    state->off_tail = 0;
    state->high_water_mark = workspace_off();
  } else {
    a0_transport_frame_hdr_t* head_hdr = twl_frame_header(twl, state->off_head);
    state->off_head = head_hdr->next_off;

    // Check whether the old head frame was responsible for the high water mark.
    size_t head_end = frame_end(twl, head_hdr->off);
    if (state->high_water_mark == head_end) {
      // The high water mark is always set by a tail element.
      state->high_water_mark = frame_end(twl, state->off_tail);
    }
  }
  state->seq_low++;

  a0_transport_writer_commit(twl);
}

A0_STATIC_INLINE
a0_err_t find_slot(a0_transport_writer_locked_t* twl, size_t frame_size, size_t* off) {
  a0_transport_hdr_t* hdr = twl_header(twl);
  a0_transport_state_t* state = twl_working_page(twl);

  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_writer_empty(twl, &empty));

  if (empty) {
    *off = workspace_off();
  } else {
    *off = max_align(frame_end(twl, state->off_tail));
    if (*off + frame_size >= hdr->arena_size) {
      *off = workspace_off();
    }
  }

  if (*off + frame_size > hdr->arena_size) {
    return A0_ERR_FRAME_LARGE;
  }

  return A0_OK;
}

A0_STATIC_INLINE
void do_evict(a0_transport_writer_locked_t* twl, size_t off, size_t frame_size) {
  size_t head_off;
  size_t head_size;
  a0_transport_state_t* state = twl_working_page(twl);
  while (head_interval(twl, state, &head_off, &head_size) &&
         frame_intersects(off, frame_size, head_off, head_size)) {
    remove_head(twl, state);
  }
}

A0_STATIC_INLINE
void slot_init(a0_transport_state_t* state,
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
void maybe_set_head(a0_transport_state_t* state, a0_transport_frame_hdr_t* frame_hdr) {
  if (!state->off_head) {
    state->off_head = frame_hdr->off;
  }
}

A0_STATIC_INLINE
void update_tail(a0_transport_writer_locked_t* twl,
                 a0_transport_state_t* state,
                 a0_transport_frame_hdr_t* frame_hdr) {
  if (state->off_tail) {
    a0_transport_frame_hdr_t* tail_frame_hdr = twl_frame_header(twl, state->off_tail);
    tail_frame_hdr->next_off = frame_hdr->off;
    frame_hdr->prev_off = state->off_tail;
  }
  state->off_tail = frame_hdr->off;
}

A0_STATIC_INLINE
void update_high_water_mark(a0_transport_writer_locked_t* twl,
                            a0_transport_state_t* state,
                            a0_transport_frame_hdr_t* frame_hdr) {
  size_t high_water_mark = frame_end(twl, frame_hdr->off);
  if (state->high_water_mark < high_water_mark) {
    state->high_water_mark = high_water_mark;
  }
}

a0_err_t a0_transport_writer_alloc_evicts(a0_transport_writer_locked_t* twl, size_t size, bool* out) {
  size_t frame_size = sizeof(a0_transport_frame_hdr_t) + size;

  size_t off;
  A0_RETURN_ERR_ON_ERR(find_slot(twl, frame_size, &off));

  size_t head_off;
  size_t head_size;
  a0_transport_state_t* state = twl_working_page(twl);
  *out = head_interval(twl, state, &head_off, &head_size) &&
         frame_intersects(off, frame_size, head_off, head_size);

  return A0_OK;
}

a0_err_t a0_transport_writer_alloc(a0_transport_writer_locked_t* twl, size_t size, a0_transport_frame_t* frame_out) {
  if (twl->_tw->_arena.mode == A0_ARENA_MODE_READONLY) {
    return A0_MAKE_SYSERR(EPERM);
  }
  size_t frame_size = sizeof(a0_transport_frame_hdr_t) + size;

  size_t off;
  A0_RETURN_ERR_ON_ERR(find_slot(twl, frame_size, &off));

  do_evict(twl, off, frame_size);

  a0_transport_hdr_t* hdr = twl_header(twl);
  // Note: do_evict commits changes, which invalidates state.
  //       Must grab state afterwards.
  a0_transport_state_t* state = twl_working_page(twl);

  a0_transport_frame_hdr_t* frame_hdr = twl_frame_header(twl, off);
  slot_init(state, frame_hdr, off, size);

  maybe_set_head(state, frame_hdr);
  update_tail(twl, state, frame_hdr);
  update_high_water_mark(twl, state, frame_hdr);

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)hdr + off + sizeof(a0_transport_frame_hdr_t);

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t allocator_impl(void* user_data, size_t size, a0_buf_t* buf_out) {
  a0_transport_frame_t frame;
  A0_RETURN_ERR_ON_ERR(a0_transport_writer_alloc((a0_transport_writer_locked_t*)user_data, size, &frame));
  *buf_out = (a0_buf_t){frame.data, frame.hdr.data_size};
  return A0_OK;
}

a0_err_t a0_transport_writer_allocator(a0_transport_writer_locked_t* lk, a0_alloc_t* alloc_out) {
  alloc_out->user_data = lk;
  alloc_out->alloc = allocator_impl;
  alloc_out->dealloc = NULL;
  return A0_OK;
}

a0_err_t a0_transport_writer_commit(a0_transport_writer_locked_t* twl) {
  a0_transport_hdr_t* hdr = twl_header(twl);
  // Assume page A was the previously committed page and page B is the working
  // page that is ready to be committed. Both represent a valid state for the
  // transport. It's possible that the copying of B into A will fail (prog crash),
  // leaving A in an inconsistent state. We set B as the committed page, before
  // copying the page info.
  hdr->committed_page_idx = !hdr->committed_page_idx;
  *twl_working_page(twl) = *twl_committed_page(twl);

  a0_rwcnd_broadcast(&hdr->rwcnd);

  return A0_OK;
}

a0_err_t a0_transport_writer_used_space(a0_transport_writer_locked_t* twl, size_t* out) {
  a0_transport_state_t* state = twl_working_page(twl);
  *out = state->high_water_mark;
  return A0_OK;
}

a0_err_t a0_transport_writer_resize(a0_transport_writer_locked_t* twl, size_t arena_size) {
  size_t used_space;
  A0_RETURN_ERR_ON_ERR(a0_transport_writer_used_space(twl, &used_space));
  if (arena_size < used_space) {
    return A0_ERR_INVALID_ARG;
  }

  a0_transport_hdr_t* hdr = twl_header(twl);
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

void a0_transport_writer_debugstr(a0_transport_writer_locked_t* twl, a0_buf_t* out) {
  a0_transport_hdr_t* hdr = twl_header(twl);

  a0_transport_state_t* committed_state = twl_committed_page(twl);
  a0_transport_state_t* working_state = twl_working_page(twl);

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
      a0_transport_frame_hdr_t* frame_hdr = twl_frame_header(twl, off);
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
