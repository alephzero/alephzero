#include <a0/common.h>  // for a0_buf_t, errno_t, A0_OK
#include <a0/stream.h>  // for a0_locked_stream_t, a0_stream_t, a0_stream_fr...

#include <errno.h>     // for EAGAIN, ESHUTDOWN, ENOMEM, EOVERFLOW, EOWNERDEAD
#include <pthread.h>   // for pthread_cond_broadcast, pthread_cond_wait
#include <stdalign.h>  // for alignof
#include <stdbool.h>   // for bool, false, true
#include <stddef.h>    // for size_t, NULL
#include <stdint.h>    // for uint8_t, uint64_t, uint32_t, uintptr_t
#include <stdio.h>     // for fprintf, fclose, fflush, fwrite, open_memstream
#include <string.h>    // for memset, memcmp, memcpy

#include "macros.h"  // for A0_STATIC_INLINE
#include "sync.h"

typedef uintptr_t stream_off_t;  // ptr offset from start of shm.

typedef struct a0_stream_state_s {
  uint64_t seq_low;
  uint64_t seq_high;
  stream_off_t off_head;
  stream_off_t off_tail;
} a0_stream_state_t;

typedef struct a0_stream_hdr_s {
  uint32_t next_conn_id;
  bool initialized;

  pthread_mutex_t mu;
  a0_futex_t fu;

  a0_stream_state_t state_pages[2];
  uint32_t committed_page_idx;

  size_t shm_size;
  size_t protocol_name_size;
  size_t protocol_metadata_size;

  uint32_t protocol_major_version;
  uint32_t protocol_minor_version;
  uint32_t protocol_patch_version;
} a0_stream_hdr_t;

A0_STATIC_INLINE
a0_stream_state_t* a0_stream_committed_page(a0_locked_stream_t lk) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  return &hdr->state_pages[hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_stream_state_t* a0_stream_working_page(a0_locked_stream_t lk) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  return &hdr->state_pages[!hdr->committed_page_idx];
}

A0_STATIC_INLINE
stream_off_t a0_max_align(stream_off_t off) {
  return ((off + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

A0_STATIC_INLINE
stream_off_t a0_stream_protocol_name_off(a0_stream_hdr_t* hdr) {
  (void)hdr;
  return a0_max_align(sizeof(a0_stream_hdr_t));
}

A0_STATIC_INLINE
stream_off_t a0_strem_protocol_metadata_off(a0_stream_hdr_t* hdr) {
  return a0_max_align(a0_stream_protocol_name_off(hdr) + hdr->protocol_name_size);
}

A0_STATIC_INLINE
stream_off_t a0_stream_workspace_off(a0_stream_hdr_t* hdr) {
  return a0_max_align(a0_strem_protocol_metadata_off(hdr) + hdr->protocol_metadata_size);
}

errno_t a0_stream_init(a0_stream_t* stream,
                       a0_buf_t arena,
                       a0_stream_protocol_t protocol,
                       a0_stream_init_status_t* status_out,
                       a0_locked_stream_t* lk_out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)arena.ptr;

  memset(stream, 0, sizeof(a0_stream_t));
  stream->_arena = arena;
  stream->_conn_id = a0_atomic_fetch_inc(&hdr->next_conn_id);

  if (stream->_conn_id == 0) {
    stream_off_t protocol_name_off = a0_max_align(sizeof(a0_stream_hdr_t));
    stream_off_t protocol_metadata_off = a0_max_align(protocol_name_off + protocol.name.size);
    stream_off_t workspace_off = a0_max_align(protocol_metadata_off + protocol.metadata_size);

    if (workspace_off >= (uint64_t)stream->_arena.size) {
      return ENOMEM;
    }

    hdr->shm_size = stream->_arena.size;
    hdr->protocol_name_size = protocol.name.size;
    hdr->protocol_metadata_size = protocol.metadata_size;
    hdr->protocol_major_version = protocol.major_version;
    hdr->protocol_minor_version = protocol.minor_version;
    hdr->protocol_patch_version = protocol.patch_version;

    memcpy((uint8_t*)hdr + a0_stream_protocol_name_off(hdr), protocol.name.ptr, protocol.name.size);

    pthread_mutexattr_t mu_attr;
    pthread_mutexattr_init(&mu_attr);

    pthread_mutexattr_setpshared(&mu_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mu_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mu_attr, PTHREAD_MUTEX_ERRORCHECK);

    pthread_mutex_init(&hdr->mu, &mu_attr);
    pthread_mutexattr_destroy(&mu_attr);

    a0_atomic_store(&hdr->initialized, true);
    *status_out = A0_STREAM_CREATED;
    a0_lock_stream(stream, lk_out);
  } else {
    while (A0_UNLIKELY(!a0_atomic_load(&hdr->initialized))) {
      a0_cpu_relax();
    }

    a0_lock_stream(stream, lk_out);
    a0_stream_protocol_t active_protocol;
    a0_stream_protocol(*lk_out, &active_protocol, NULL);

    *status_out = A0_STREAM_PROTOCOL_MATCH;
    if (protocol.name.size != active_protocol.name.size ||
        memcmp(protocol.name.ptr, active_protocol.name.ptr, protocol.name.size) != 0 ||
        protocol.major_version != active_protocol.major_version ||
        protocol.minor_version != active_protocol.minor_version ||
        protocol.patch_version != active_protocol.patch_version ||
        protocol.metadata_size != active_protocol.metadata_size) {
      *status_out = A0_STREAM_PROTOCOL_MISMATCH;
    }
  }

  return A0_OK;
}

errno_t a0_stream_close(a0_stream_t* stream) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)stream->_arena.ptr;

  a0_locked_stream_t lk;
  a0_lock_stream(stream, &lk);

  if (!stream->_closing) {
    stream->_closing = true;

    hdr->fu = stream->_conn_id;
    a0_futex_broadcast(&hdr->fu);

    hdr->fu = stream->_conn_id;
    while (stream->_await_cnt) {
      a0_unlock_stream(lk);
      a0_futex_wait(&hdr->fu, stream->_conn_id, NULL);
      a0_lock_stream(stream, &lk);
      hdr->fu = stream->_conn_id;
    }
  }

  a0_unlock_stream(lk);

  return A0_OK;
}

errno_t a0_lock_stream(a0_stream_t* stream, a0_locked_stream_t* lk_out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)stream->_arena.ptr;

  lk_out->stream = stream;

  errno_t lock_status = pthread_mutex_lock(&hdr->mu);
  if (lock_status == EOWNERDEAD) {
    // The data is always consistent by design.
    lock_status = pthread_mutex_consistent(&hdr->mu);
    hdr->fu = stream->_conn_id;
    a0_futex_broadcast(&hdr->fu);
  }

  // Clear any incomplete changes.
  *a0_stream_working_page(*lk_out) = *a0_stream_committed_page(*lk_out);

  return lock_status;
}

errno_t a0_unlock_stream(a0_locked_stream_t lk) {
  *a0_stream_working_page(lk) = *a0_stream_committed_page(lk);
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  pthread_mutex_unlock(&hdr->mu);
  return A0_OK;
}

errno_t a0_stream_protocol(a0_locked_stream_t lk,
                           a0_stream_protocol_t* protocol_out,
                           a0_buf_t* metadata_out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  if (protocol_out) {
    protocol_out->name.ptr = (uint8_t*)hdr + a0_stream_protocol_name_off(hdr);
    protocol_out->name.size = hdr->protocol_name_size;
    protocol_out->metadata_size = hdr->protocol_metadata_size;
    protocol_out->major_version = hdr->protocol_major_version;
    protocol_out->minor_version = hdr->protocol_minor_version;
    protocol_out->patch_version = hdr->protocol_patch_version;
  }
  if (metadata_out) {
    metadata_out->ptr = (uint8_t*)hdr + a0_strem_protocol_metadata_off(hdr);
    metadata_out->size = hdr->protocol_metadata_size;
  }
  return A0_OK;
}

errno_t a0_stream_empty(a0_locked_stream_t lk, bool* out) {
  a0_stream_state_t* working_page = a0_stream_working_page(lk);
  *out = !working_page->seq_high || working_page->seq_low > working_page->seq_high;
  return A0_OK;
}

errno_t a0_stream_nonempty(a0_locked_stream_t lk, bool* out) {
  errno_t err = a0_stream_empty(lk, out);
  *out = !*out;
  return err;
}

errno_t a0_stream_jump_head(a0_locked_stream_t lk) {
  a0_stream_state_t* state = a0_stream_working_page(lk);

  bool empty;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_empty(lk, &empty));
  if (empty) {
    return EAGAIN;
  }

  lk.stream->_seq = state->seq_low;
  lk.stream->_off = state->off_head;
  return A0_OK;
}

errno_t a0_stream_jump_tail(a0_locked_stream_t lk) {
  a0_stream_state_t* state = a0_stream_working_page(lk);

  bool empty;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_empty(lk, &empty));
  if (empty) {
    return EAGAIN;
  }

  lk.stream->_seq = state->seq_high;
  lk.stream->_off = state->off_tail;
  return A0_OK;
}

errno_t a0_stream_has_next(a0_locked_stream_t lk, bool* out) {
  bool empty;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_empty(lk, &empty));

  a0_stream_state_t* state = a0_stream_working_page(lk);
  *out = !empty && (lk.stream->_seq < state->seq_high);
  return A0_OK;
}

errno_t a0_stream_next(a0_locked_stream_t lk) {
  a0_stream_state_t* state = a0_stream_working_page(lk);

  bool has_next;
  A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_has_next(lk, &has_next));
  if (!has_next) {
    return EAGAIN;
  }

  if (lk.stream->_seq < state->seq_low) {
    lk.stream->_seq = state->seq_low;
    lk.stream->_off = state->off_head;
    return A0_OK;
  }

  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  a0_stream_frame_hdr_t* frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + lk.stream->_off);
  lk.stream->_seq = frame_hdr->seq + 1;
  lk.stream->_off = frame_hdr->next_off;

  return A0_OK;
}

errno_t a0_stream_await(a0_locked_stream_t lk, errno_t (*pred)(a0_locked_stream_t, bool*)) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;

  if (lk.stream->_closing) {
    return ESHUTDOWN;
  }

  bool sat = false;
  errno_t err = pred(lk, &sat);
  if (err || sat) {
    return err;
  }

  lk.stream->_await_cnt++;

  hdr->fu = lk.stream->_conn_id;
  while (!lk.stream->_closing) {
    err = pred(lk, &sat);
    if (err || sat) {
      break;
    }
    a0_futex_broadcast(&hdr->fu);
    a0_unlock_stream(lk);
    a0_futex_wait(&hdr->fu, lk.stream->_conn_id, NULL);
    a0_lock_stream(lk.stream, &lk);
    hdr->fu = lk.stream->_conn_id;
  }
  if (!err && lk.stream->_closing) {
    err = ESHUTDOWN;
  }

  lk.stream->_await_cnt--;
  hdr->fu = lk.stream->_conn_id;
  a0_futex_broadcast(&hdr->fu);

  return err;
}

errno_t a0_stream_frame(a0_locked_stream_t lk, a0_stream_frame_t* frame_out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  a0_stream_state_t* state = a0_stream_working_page(lk);

  if (lk.stream->_seq < state->seq_low) {
    return ESPIPE;
  }

  a0_stream_frame_hdr_t* frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + lk.stream->_off);

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)frame_hdr + sizeof(a0_stream_frame_hdr_t);
  return A0_OK;
}

A0_STATIC_INLINE
stream_off_t a0_stream_frame_end(a0_stream_hdr_t* hdr, stream_off_t frame_off) {
  a0_stream_frame_hdr_t* frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + frame_off);
  return frame_off + sizeof(a0_stream_frame_hdr_t) + frame_hdr->data_size;
}

A0_STATIC_INLINE
bool a0_stream_frame_intersects(stream_off_t frame1_start,
                                size_t frame1_size,
                                stream_off_t frame2_start,
                                size_t frame2_size) {
  stream_off_t frame1_end = frame1_start + frame1_size;
  stream_off_t frame2_end = frame2_start + frame2_size;
  return (frame1_start < frame2_end) && (frame2_start < frame1_end);
}

A0_STATIC_INLINE
bool a0_stream_head_interval(a0_locked_stream_t lk, stream_off_t* head_off, size_t* head_size) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;

  bool empty;
  a0_stream_empty(lk, &empty);
  if (empty) {
    return false;
  }

  *head_off = a0_stream_working_page(lk)->off_head;
  a0_stream_frame_hdr_t* head_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + *head_off);
  *head_size = sizeof(a0_stream_frame_hdr_t) + head_hdr->data_size;
  return true;
}

A0_STATIC_INLINE
void a0_stream_remove_head(a0_locked_stream_t lk) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  a0_stream_state_t* state = a0_stream_working_page(lk);

  a0_stream_frame_hdr_t* head_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + state->off_head);

  if (state->off_head == state->off_tail) {
    state->off_head = 0;
    state->off_tail = 0;
    state->seq_low++;
  } else {
    head_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + head_hdr->next_off);
    state->off_head = head_hdr->off;
    state->seq_low = head_hdr->seq;
    head_hdr->prev_off = 0;
  }
  a0_stream_commit(lk);
}

errno_t a0_stream_alloc(a0_locked_stream_t lk, size_t size, a0_stream_frame_t* frame_out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;

  a0_stream_state_t* state = a0_stream_working_page(lk);

  stream_off_t off;
  size_t frame_size = sizeof(a0_stream_frame_hdr_t) + size;

  if (!state->off_head) {
    off = a0_stream_workspace_off(hdr);
  } else {
    off = a0_max_align(a0_stream_frame_end(hdr, state->off_tail));
    if (off + frame_size >= hdr->shm_size) {
      off = a0_stream_workspace_off(hdr);
    }
  }

  if (off + frame_size >= hdr->shm_size) {
    return EOVERFLOW;
  }

  stream_off_t head_off;
  size_t head_size;
  while (a0_stream_head_interval(lk, &head_off, &head_size) &&
         a0_stream_frame_intersects(off, frame_size, head_off, head_size)) {
    a0_stream_remove_head(lk);
  }

  // Note: a0_stream_remove_head commits changes, which invalidates state.
  state = a0_stream_working_page(lk);

  a0_stream_frame_hdr_t* frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + off);
  memset(frame_hdr, 0, sizeof(a0_stream_frame_hdr_t));
  frame_hdr->seq = ++state->seq_high;
  frame_hdr->off = off;
  frame_hdr->next_off = 0;
  frame_hdr->data_size = size;

  if (!state->off_head) {
    state->off_head = off;
  }
  if (state->off_tail) {
    a0_stream_frame_hdr_t* tail_frame_hdr =
        (a0_stream_frame_hdr_t*)((uint8_t*)hdr + state->off_tail);
    tail_frame_hdr->next_off = off;
    frame_hdr->prev_off = state->off_tail;
  }
  state->off_tail = off;
  if (!state->seq_low) {
    state->seq_low = frame_hdr->seq;
  }

  frame_out->hdr = *frame_hdr;
  frame_out->data = (uint8_t*)hdr + off + sizeof(a0_stream_frame_hdr_t);

  return A0_OK;
}

errno_t a0_stream_commit(a0_locked_stream_t lk) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;
  // Assume page A was the previously committed page and page B is the working
  // page that is ready to be committed. Both represent a valid state for the
  // stream. It's possible that the copying of B into A will fail (prog crash),
  // leaving A in an inconsistent state. We set B as the committed page, before
  // copying the page info.
  hdr->committed_page_idx = !hdr->committed_page_idx;
  *a0_stream_working_page(lk) = *a0_stream_committed_page(lk);

  hdr->fu = lk.stream->_conn_id;
  a0_futex_broadcast(&hdr->fu);

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
  fwrite(str.ptr, sizeof(char), line_size, f);
  if (overflow) {
    fprintf(f, "...");
  }
}

void a0_stream_debugstr(a0_locked_stream_t lk, a0_buf_t* out) {
  a0_stream_hdr_t* hdr = (a0_stream_hdr_t*)lk.stream->_arena.ptr;

  a0_stream_state_t* committed_state = a0_stream_committed_page(lk);
  a0_stream_state_t* working_state = a0_stream_working_page(lk);

  FILE* ss = open_memstream((char**)&out->ptr, &out->size);
  // clang-format off
  fprintf(ss, "\n{\n");
  fprintf(ss, "  \"header\": {\n");
  fprintf(ss, "    \"shm_size\": %lu,\n", hdr->shm_size);
  fprintf(ss, "    \"committed_state\": {\n");
  fprintf(ss, "      \"seq_low\": %lu,\n", committed_state->seq_low);
  fprintf(ss, "      \"seq_high\": %lu,\n", committed_state->seq_high);
  fprintf(ss, "      \"off_head\": %lu,\n", committed_state->off_head);
  fprintf(ss, "      \"off_tail\": %lu\n", committed_state->off_tail);
  fprintf(ss, "    },\n");
  fprintf(ss, "    \"working_state\": {\n");
  fprintf(ss, "      \"seq_low\": %lu,\n", working_state->seq_low);
  fprintf(ss, "      \"seq_high\": %lu,\n", working_state->seq_high);
  fprintf(ss, "      \"off_head\": %lu,\n", working_state->off_head);
  fprintf(ss, "      \"off_tail\": %lu\n", working_state->off_tail);
  fprintf(ss, "    }\n");
  fprintf(ss, "  },\n");
  fprintf(ss, "  \"protocol\": {\n");
  fprintf(ss, "    \"name\": \"%s\",\n", (char*)hdr + a0_stream_protocol_name_off(hdr));
  fprintf(ss, "    \"semver\": \"%d.%d.%d\",\n", hdr->protocol_major_version, hdr->protocol_minor_version, hdr->protocol_patch_version);
  fprintf(ss, "    \"metadata_size\": %ld,\n", hdr->protocol_metadata_size);
  a0_buf_t metadata = {
      .ptr = (uint8_t*)hdr + a0_strem_protocol_metadata_off(hdr),
      .size = hdr->protocol_metadata_size,
  };
  fprintf(ss, "    \"metadata\": \"");  write_limited(ss, metadata); fprintf(ss, "\"\n");
  fprintf(ss, "  },\n");
  fprintf(ss, "  \"data\": [\n");
  // clang-format on

  if (working_state->off_head) {
    uint64_t off = working_state->off_head;
    a0_stream_frame_hdr_t* frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + off);
    uint64_t seq = frame_hdr->seq;
    bool first = true;
    while (true) {
      frame_hdr = (a0_stream_frame_hdr_t*)((uint8_t*)hdr + off);

      seq = frame_hdr->seq;
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
          .ptr = (uint8_t*)hdr + frame_hdr->off + sizeof(a0_stream_frame_hdr_t),
          .size = frame_hdr->data_size,
      };
      fprintf(ss, "      \"data\": \"");  write_limited(ss, data); fprintf(ss, "\"\n");

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
