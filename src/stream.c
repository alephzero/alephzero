#include <a0/stream.h>

#include <fcntl.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

const int64_t A0_STREAM_MAGIC = 0xA0A0A0A0A0A0A0A0;
typedef uintptr_t fcl_off_t;    // ptr offset from start of shmobj.

typedef struct a0_fcl_state_s {
  uint64_t seq_low;
  uint64_t seq_high;
  fcl_off_t off_head;
  fcl_off_t off_tail;
} a0_fcl_state_t;

typedef struct a0_fcl_hdr_s {
  uint64_t magic;

  pthread_mutex_t mu;
  // This should be a pthread_cond_t, but that's broken (╯°□°)╯︵ ┻━┻
  // https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=884776
  uint32_t fu_cond_var;

  a0_fcl_state_t state_pages[2];
  uint32_t committed_page_idx;

  size_t shmobj_size;
  size_t protocol_metadata_size;
} a0_fcl_hdr_t;

a0_fcl_state_t* a0_fcl_committed_page(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  return &hdr->state_pages[hdr->committed_page_idx];
}

a0_fcl_state_t* a0_fcl_working_page(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  return &hdr->state_pages[!hdr->committed_page_idx];
}

fcl_off_t a0_fcl_align(fcl_off_t off) {
  return ((off + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

fcl_off_t a0_fcl_protocol_metadata_off(a0_fcl_hdr_t* hdr) {
  return a0_fcl_align(sizeof(a0_fcl_hdr_t));
}

fcl_off_t a0_fcl_workspace_off(a0_fcl_hdr_t* hdr) {
  return a0_fcl_align(a0_fcl_protocol_metadata_off(hdr) + hdr->protocol_metadata_size);
}

errno_t _a0_futex(uint32_t* uaddr, uint32_t futex_op, uint32_t val, const struct timespec* timeout, uint32_t* uaddr2, uint32_t val3) {
  _A0_RETURN_ERR_ON_MINUS_ONE(syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3));
  return A0_OK;
}

errno_t a0_futex_await_change(uint32_t* uaddr, uint32_t old_val) {
  return _a0_futex(uaddr, FUTEX_WAIT, old_val, NULL, NULL, 0);
}

errno_t a0_futex_notify_change(uint32_t* uaddr) {
  return _a0_futex(uaddr, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}

typedef struct a0_fcl_elem_hdr_s {
  uint64_t seq;
  uint64_t off;
  uint64_t next_off;
  uint64_t payload_size;
} a0_fcl_elem_hdr_t;

errno_t a0_stream_init(a0_stream_t* stream, const a0_stream_options_t* opts) {
  stream->_opts = *opts;
  stream->_seq = 0;
  stream->_off = 0;

  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)opts->shmobj->ptr;
  if (hdr->magic != A0_STREAM_MAGIC) {
    size_t protocol_metadata_size = 0;
    if (opts->construct_opts) {
      protocol_metadata_size = opts->construct_opts->protocol_metadata_size;
    }
    if (opts->shmobj->stat.st_size < sizeof(a0_fcl_hdr_t) + protocol_metadata_size + alignof(max_align_t)) {
      return ENOMEM;
    }

    memset((uint8_t*)hdr, 0, sizeof(a0_fcl_hdr_t));

    hdr->shmobj_size = opts->shmobj->stat.st_size;

    pthread_mutexattr_t mu_attr;
    pthread_mutexattr_init(&mu_attr);

    pthread_mutexattr_setpshared(&mu_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mu_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mu_attr, PTHREAD_MUTEX_ERRORCHECK);

    pthread_mutex_init(&hdr->mu, &mu_attr);
    pthread_mutexattr_destroy(&mu_attr);

    if (opts->construct_opts) {
      hdr->protocol_metadata_size = opts->construct_opts->protocol_metadata_size;
      if (opts->construct_opts->on_construct) {
        opts->construct_opts->on_construct(stream);
      }
    }

    hdr->magic = A0_STREAM_MAGIC;
  } else if (opts->construct_opts && opts->construct_opts->on_already_constructed) {
    opts->construct_opts->on_already_constructed(stream);
  }

  return A0_OK;
}

errno_t a0_stream_close(a0_stream_t* stream) {
  return A0_OK;
}

errno_t a0_lock_stream(a0_locked_stream_t* lk, a0_stream_t* stream) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)stream->_opts.shmobj->ptr;

  lk->stream = stream;

  errno_t lock_status = pthread_mutex_lock(&hdr->mu);
  if (lock_status == EOWNERDEAD) {
    // Always consistent by design.
    lock_status = pthread_mutex_consistent(&hdr->mu);
    hdr->fu_cond_var++;
    a0_futex_notify_change(&hdr->fu_cond_var);
  }

  *a0_fcl_working_page(lk) = *a0_fcl_committed_page(lk);

  return lock_status;
}

errno_t a0_unlock_stream(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  pthread_mutex_unlock(&hdr->mu);
  return A0_OK;
}

errno_t a0_stream_protocol_metadata(a0_locked_stream_t* lk, a0_buf_t* out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  out->ptr = (uint8_t*)hdr + a0_fcl_protocol_metadata_off(hdr);
  out->len = hdr->protocol_metadata_size;
  return A0_OK;
}

errno_t a0_stream_is_empty(a0_locked_stream_t* lk, bool* out) {
  *out = !a0_fcl_working_page(lk)->seq_high;
  return A0_OK;
}

errno_t a0_stream_await_nonempty(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;

  uint32_t old_val = hdr->fu_cond_var;
  while (a0_fcl_committed_page(lk)->seq_high) {
    pthread_mutex_unlock(&hdr->mu);
    a0_futex_await_change(&hdr->fu_cond_var, old_val);
    pthread_mutex_lock(&hdr->mu);
    old_val = hdr->fu_cond_var;
  }
  return A0_OK;
}

errno_t a0_stream_jump_head(a0_locked_stream_t* lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (!state->seq_low) {
    return EAGAIN;
  }

  lk->stream->_seq = state->seq_low;
  lk->stream->_off = state->off_head;
  return A0_OK;
}

errno_t a0_stream_jump_tail(a0_locked_stream_t* lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (!state->seq_high) {
    return EAGAIN;
  }

  lk->stream->_seq = state->seq_high;
  lk->stream->_off = state->off_tail;
  return A0_OK;
}

errno_t a0_stream_has_next(a0_locked_stream_t* lk, bool* out) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);
  *out = (state->seq_high) && (lk->stream->_seq < state->seq_high);
  return A0_OK;
}

errno_t a0_stream_next(a0_locked_stream_t* lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (lk->stream->_seq < state->seq_low) {
    lk->stream->_seq = state->seq_low;
    lk->stream->_off = state->off_head;
    return A0_OK;
  }

  if (lk->stream->_seq == state->seq_high) {
    return EAGAIN;
  }

  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + lk->stream->_off);
  lk->stream->_seq++;
  lk->stream->_off = elem_hdr->next_off;

  return A0_OK;
}

errno_t a0_stream_await_next(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;

  uint32_t old_val = hdr->fu_cond_var;
  while (a0_fcl_committed_page(lk)->seq_high && lk->stream->_seq < a0_fcl_committed_page(lk)->seq_high) {
    pthread_mutex_unlock(&hdr->mu);
    a0_futex_await_change(&hdr->fu_cond_var, old_val);
    pthread_mutex_lock(&hdr->mu);
    old_val = hdr->fu_cond_var;
  }
  return A0_OK;
}

errno_t a0_stream_elem(a0_locked_stream_t* lk, a0_stream_elem_hdr_t* hdr_out, a0_buf_t* payload_out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (lk->stream->_seq < state->seq_low) {
    return ESPIPE;
  }

  a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + lk->stream->_off);

  hdr_out->seq = elem_hdr->seq;
  payload_out->ptr = (uint8_t*)elem_hdr + sizeof(a0_fcl_elem_hdr_t);
  payload_out->len = elem_hdr->payload_size;
  return A0_OK;
}

fcl_off_t a0_fcl_elem_end(a0_fcl_hdr_t* hdr, fcl_off_t elem_off) {
  a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + elem_off);
  return elem_off + sizeof(a0_fcl_elem_hdr_t) + elem_hdr->payload_size;
}

bool a0_fcl_intersects(fcl_off_t elem1_start, size_t elem1_size,
                       fcl_off_t elem2_start, size_t elem2_size) {
  fcl_off_t elem1_end = elem1_start + elem1_size;
  fcl_off_t elem2_end = elem2_start + elem2_size;
  return (elem1_start <= elem2_end) && (elem2_start <= elem1_end);
}

bool a0_fcl_head_interval(a0_locked_stream_t* lk, fcl_off_t* head_off, size_t* head_size) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;

  bool is_empty;
  a0_stream_is_empty(lk, &is_empty);
  if (is_empty) {
    return false;
  }

  *head_off = a0_fcl_working_page(lk)->off_head;
  a0_fcl_elem_hdr_t* head_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + *head_off);
  *head_size = sizeof(a0_fcl_elem_hdr_t) + head_hdr->payload_size;
  return true;
}

void a0_fcl_remove_head(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  a0_fcl_elem_hdr_t* head_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + state->off_head);

  state->seq_low++;
  if (state->off_head == state->off_tail) {
    state->off_head = 0;
    state->off_tail = 0;
  } else {
    state->off_head = head_hdr->next_off;
  }
  a0_stream_commit(lk);
}

errno_t a0_stream_alloc(a0_locked_stream_t* lk, size_t size, a0_buf_t* out) {
  // TODO: Check if payload fits.
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;

  a0_fcl_state_t* state = a0_fcl_working_page(lk);
  
  fcl_off_t off;
  size_t elem_size = sizeof(a0_fcl_elem_hdr_t) + size;

  if (!state->off_head) {
    off = a0_fcl_workspace_off(hdr);
    state->off_head = off;
  } else {
    off = a0_fcl_align(a0_fcl_elem_end(hdr, state->off_tail));
    if (off + elem_size >= hdr->shmobj_size) {
      off = a0_fcl_workspace_off(hdr);
    }
  }

  if (off + elem_size >= hdr->shmobj_size) {
    return EOVERFLOW;
  }

  fcl_off_t head_off;
  size_t head_size;
  while (a0_fcl_head_interval(lk, &head_off, &head_size) &&
          a0_fcl_intersects(off, elem_size, head_off, head_size)) {
    a0_fcl_remove_head(lk);
  }

  // Note: a0_fcl_remove_head commits changes, which invalidates state.
  state = a0_fcl_working_page(lk);

  a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + off);
  elem_hdr->seq = ++state->seq_high;
  elem_hdr->off = off;
  elem_hdr->payload_size = size;

  if (state->off_tail) {
    a0_fcl_elem_hdr_t* tail_elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + state->off_tail);
    tail_elem_hdr->next_off = off;
  }
  state->off_tail = off;
  if (!state->seq_low) {
    state->seq_low = elem_hdr->seq;
  }

  out->ptr = (uint8_t*)hdr + off + sizeof(a0_fcl_elem_hdr_t);
  out->len = size;

  return A0_OK;
}

errno_t a0_stream_commit(a0_locked_stream_t* lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;
  hdr->committed_page_idx = !hdr->committed_page_idx;
  *a0_fcl_working_page(lk) = *a0_fcl_committed_page(lk);

  hdr->fu_cond_var++;
  a0_futex_notify_change(&hdr->fu_cond_var);

  return A0_OK;
}

void _a0_testing_stream_debugstr(a0_locked_stream_t* lk, char** out_str, size_t* out_size) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk->stream->_opts.shmobj->ptr;

  a0_fcl_state_t* committed_state = a0_fcl_committed_page(lk);
  a0_fcl_state_t* working_state = a0_fcl_working_page(lk);

  FILE* ss = open_memstream(out_str, out_size);
  fprintf(ss, "\n=========================\n");
  fprintf(ss, "HEADER\n");
  fprintf(ss, "-------------------------\n");
  fprintf(ss, "-- shmobj_size = %lu\n", hdr->shmobj_size);
  fprintf(ss, "-------------------------\n");
  fprintf(ss, "Committed state\n"); 
  fprintf(ss, "-- seq    = [%lu, %lu]\n", committed_state->seq_low, committed_state->seq_high);
  fprintf(ss, "-- head @ = %lu\n", committed_state->off_head);
  fprintf(ss, "-- tail @ = %lu\n", committed_state->off_tail);
  fprintf(ss, "-------------------------\n");
  fprintf(ss, "Working state\n");
  fprintf(ss, "-- seq    = [%lu, %lu]\n", working_state->seq_low, working_state->seq_high);
  fprintf(ss, "-- head @ = %lu\n", working_state->off_head);
  fprintf(ss, "-- tail @ = %lu\n", working_state->off_tail);
  fprintf(ss, "=========================\n");
  fprintf(ss, "PROTOCOL INFO\n");
  fprintf(ss, "-------------------------\n");
  fprintf(ss, "-- size = %lu\n", hdr->protocol_metadata_size);
  if (hdr->protocol_metadata_size <= 32) {
    fprintf(ss, "-- payload: %.*s\n", (int)hdr->protocol_metadata_size, (char*)hdr + a0_fcl_protocol_metadata_off(hdr));
  } else {
    fprintf(ss, "-- payload: %.*s...\n", 29, (char*)hdr + a0_fcl_protocol_metadata_off(hdr));
  }
  fprintf(ss, "=========================\n");
  fprintf(ss, "DATA\n");
  if (working_state->off_head) {
    uint64_t off = working_state->off_head;
    a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + off);
    uint64_t seq = elem_hdr->seq;
    while (seq <= working_state->seq_high) {
      elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + off);
      fprintf(ss, "-------------------------\n");
      if (seq > committed_state->seq_high) {
        fprintf(ss, "Elem (not committed)\n");
      } else {
        fprintf(ss, "Elem\n");
      }
      fprintf(ss, "-- @      = %lu\n", elem_hdr->off);
      fprintf(ss, "-- seq    = %lu\n", elem_hdr->seq);
      fprintf(ss, "-- next @ = %lu\n", elem_hdr->next_off);
      fprintf(ss, "-- size   = %lu\n", elem_hdr->payload_size);
      if (elem_hdr->payload_size <= 32) {
        fprintf(ss, "-- payload: %.*s\n", (int)elem_hdr->payload_size, (char*)hdr + elem_hdr->off + sizeof(a0_fcl_elem_hdr_t));
      } else {
        fprintf(ss, "-- payload: %.*s...\n", 29, (char*)hdr + elem_hdr->off + sizeof(a0_fcl_elem_hdr_t));
      }

      off = elem_hdr->next_off;
      seq++;
    }
  }
  fprintf(ss, "=========================\n");
  fflush(ss);
  fclose(ss);
}
