#include <a0/stream.h>

#include <a0/internal/macros.h>
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

// TODO: s/fcl/stream/g

const uint64_t A0_STREAM_MAGIC = 0x616c65667a65726f;
typedef uintptr_t fcl_off_t;  // ptr offset from start of shmobj.

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
  size_t protocol_name_size;
  size_t protocol_metadata_size;

  uint32_t protocol_major_version;
  uint32_t protocol_minor_version;
  uint32_t protocol_patch_version;
} a0_fcl_hdr_t;

A0_STATIC_INLINE
a0_fcl_state_t* a0_fcl_committed_page(a0_locked_stream_t lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  return &hdr->state_pages[hdr->committed_page_idx];
}

A0_STATIC_INLINE
a0_fcl_state_t* a0_fcl_working_page(a0_locked_stream_t lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  return &hdr->state_pages[!hdr->committed_page_idx];
}

A0_STATIC_INLINE
fcl_off_t a0_fcl_align(fcl_off_t off) {
  return ((off + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1));
}

A0_STATIC_INLINE
fcl_off_t a0_fcl_protocol_name_off(a0_fcl_hdr_t* hdr) {
  return a0_fcl_align(sizeof(a0_fcl_hdr_t));
}

A0_STATIC_INLINE
fcl_off_t a0_fcl_protocol_metadata_off(a0_fcl_hdr_t* hdr) {
  return a0_fcl_align(a0_fcl_protocol_name_off(hdr) +
                      hdr->protocol_name_size);
}

A0_STATIC_INLINE
fcl_off_t a0_fcl_workspace_off(a0_fcl_hdr_t* hdr) {
  return a0_fcl_align(a0_fcl_protocol_metadata_off(hdr) +
                      hdr->protocol_metadata_size);
}

A0_STATIC_INLINE
errno_t a0_futex(uint32_t* uaddr,
                 uint32_t futex_op,
                 uint32_t val,
                 const struct timespec* timeout,
                 uint32_t* uaddr2,
                 uint32_t val3) {
  A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(
      syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3));
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_futex_await_change(uint32_t* uaddr, uint32_t old_val) {
  return a0_futex(uaddr, FUTEX_WAIT, old_val, NULL, NULL, 0);
}

A0_STATIC_INLINE
errno_t a0_futex_notify_change(uint32_t* uaddr) {
  return a0_futex(uaddr, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}

// TODO: Consider merging this with a0_stream_frame_hdr_t.
typedef struct a0_fcl_elem_hdr_s {
  uint64_t seq;
  uint64_t off;
  uint64_t next_off;
  uint64_t data_size;
} a0_fcl_elem_hdr_t;

errno_t a0_stream_init(a0_stream_t* stream,
                       a0_shmobj_t shmobj,
                       a0_stream_protocol_t protocol,
                       a0_stream_init_status_t* status_out,
                       a0_locked_stream_t* lk_out) {
  memset(stream, 0, sizeof(a0_stream_t));
  stream->_shmobj = shmobj;

  pthread_mutex_init(&stream->_await_mu, NULL);
  pthread_cond_init(&stream->_await_cv, NULL);

  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)shmobj.ptr;
  if (hdr->magic != A0_STREAM_MAGIC) {
    // TODO: fcntl with F_SETLKW and check double-check magic.

    fcl_off_t protocol_name_off = a0_fcl_align(sizeof(a0_fcl_hdr_t));
    fcl_off_t protocol_metadata_off = a0_fcl_align(protocol_name_off + protocol.name.size);
    fcl_off_t workspace_off = a0_fcl_align(protocol_metadata_off + protocol.metadata_size);

    if (workspace_off >= (uint64_t)stream->_shmobj.stat.st_size) {
      return ENOMEM;
    }

    memset((uint8_t*)hdr, 0, sizeof(a0_fcl_hdr_t));

    hdr->shmobj_size = stream->_shmobj.stat.st_size;
    hdr->protocol_name_size = protocol.name.size;
    hdr->protocol_metadata_size = protocol.metadata_size;
    hdr->protocol_major_version = protocol.major_version;
    hdr->protocol_minor_version = protocol.minor_version;
    hdr->protocol_patch_version = protocol.patch_version;

    memcpy((uint8_t*)hdr + a0_fcl_protocol_name_off(hdr), protocol.name.ptr, protocol.name.size);

    pthread_mutexattr_t mu_attr;
    pthread_mutexattr_init(&mu_attr);

    pthread_mutexattr_setpshared(&mu_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mu_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mu_attr, PTHREAD_MUTEX_ERRORCHECK);

    pthread_mutex_init(&hdr->mu, &mu_attr);
    pthread_mutexattr_destroy(&mu_attr);

    hdr->magic = A0_STREAM_MAGIC;
    *status_out = A0_STREAM_CREATED;
    a0_lock_stream(stream, lk_out);
  } else {
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
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)stream->_shmobj.ptr;

  pthread_mutex_lock(&stream->_await_mu);

  if (!stream->_closing) {
    stream->_closing = true;

    hdr->fu_cond_var++;
    a0_futex_notify_change(&hdr->fu_cond_var);

    while (stream->_await_cnt) {
      pthread_cond_wait(&stream->_await_cv, &stream->_await_mu);
    }
  }

  pthread_mutex_unlock(&stream->_await_mu);
  return A0_OK;
}

errno_t a0_lock_stream(a0_stream_t* stream, a0_locked_stream_t* lk_out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)stream->_shmobj.ptr;

  lk_out->stream = stream;

  errno_t lock_status = pthread_mutex_lock(&hdr->mu);
  if (lock_status == EOWNERDEAD) {
    // Always consistent by design.
    lock_status = pthread_mutex_consistent(&hdr->mu);
    hdr->fu_cond_var++;
    a0_futex_notify_change(&hdr->fu_cond_var);
  }

  *a0_fcl_working_page(*lk_out) = *a0_fcl_committed_page(*lk_out);

  return lock_status;
}

errno_t a0_unlock_stream(a0_locked_stream_t lk) {
  *a0_fcl_working_page(lk) = *a0_fcl_committed_page(lk);
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  pthread_mutex_unlock(&hdr->mu);
  return A0_OK;
}

errno_t a0_stream_protocol(a0_locked_stream_t lk, a0_stream_protocol_t* protocol_out, a0_buf_t* metadata_out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  if (protocol_out) {
    protocol_out->name.ptr = (uint8_t*)hdr + a0_fcl_protocol_name_off(hdr);
    protocol_out->name.size = hdr->protocol_name_size;
    protocol_out->metadata_size = hdr->protocol_metadata_size;
    protocol_out->major_version = hdr->protocol_major_version;
    protocol_out->minor_version = hdr->protocol_minor_version;
    protocol_out->patch_version = hdr->protocol_patch_version;
  }
  if (metadata_out) {
    metadata_out->ptr = (uint8_t*)hdr + a0_fcl_protocol_metadata_off(hdr);
    metadata_out->size = hdr->protocol_metadata_size;
  }
  return A0_OK;
}

errno_t a0_stream_empty(a0_locked_stream_t lk, bool* out) {
  *out = !a0_fcl_working_page(lk)->seq_high;
  return A0_OK;
}

errno_t a0_stream_nonempty(a0_locked_stream_t lk, bool* out) {
  errno_t err = a0_stream_empty(lk, out);
  *out = !*out;
  return err;
}

errno_t a0_stream_jump_head(a0_locked_stream_t lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (!state->seq_low) {
    return EAGAIN;
  }

  lk.stream->_seq = state->seq_low;
  lk.stream->_off = state->off_head;
  return A0_OK;
}

errno_t a0_stream_jump_tail(a0_locked_stream_t lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (!state->seq_high) {
    return EAGAIN;
  }

  lk.stream->_seq = state->seq_high;
  lk.stream->_off = state->off_tail;
  return A0_OK;
}

errno_t a0_stream_has_next(a0_locked_stream_t lk, bool* out) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);
  *out = (state->seq_high) && (lk.stream->_seq < state->seq_high);
  return A0_OK;
}

errno_t a0_stream_next(a0_locked_stream_t lk) {
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (lk.stream->_seq < state->seq_low) {
    lk.stream->_seq = state->seq_low;
    lk.stream->_off = state->off_head;
    return A0_OK;
  }

  if (lk.stream->_seq == state->seq_high) {
    return EAGAIN;
  }

  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  a0_fcl_elem_hdr_t* elem_hdr =
      (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + lk.stream->_off);
  lk.stream->_seq++;
  lk.stream->_off = elem_hdr->next_off;

  return A0_OK;
}

errno_t a0_stream_await(a0_locked_stream_t lk,
                        errno_t (*pred)(a0_locked_stream_t, bool*)) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  errno_t err = A0_OK;

  if (lk.stream->_closing) {
    return ESHUTDOWN;
  }
  pthread_mutex_lock(&lk.stream->_await_mu);
  lk.stream->_await_cnt++;
  pthread_mutex_unlock(&lk.stream->_await_mu);

  uint32_t old_val = hdr->fu_cond_var;
  while (true) {
    if (lk.stream->_closing) {
      break;
    }

    bool sat;
    err = pred(lk, &sat);
    if (err || sat) {
      break;
    }
    pthread_mutex_unlock(&hdr->mu);
    a0_futex_await_change(&hdr->fu_cond_var, old_val);
    pthread_mutex_lock(&hdr->mu);
    old_val = hdr->fu_cond_var;
  }
  if (lk.stream->_closing) {
    err = ESHUTDOWN;
  }

  pthread_mutex_lock(&lk.stream->_await_mu);
  lk.stream->_await_cnt--;
  pthread_mutex_unlock(&lk.stream->_await_mu);
  pthread_cond_broadcast(&lk.stream->_await_cv);

  return err;
}

errno_t a0_stream_frame(a0_locked_stream_t lk, a0_stream_frame_t* frame_out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  if (lk.stream->_seq < state->seq_low) {
    return ESPIPE;
  }

  a0_fcl_elem_hdr_t* elem_hdr =
      (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + lk.stream->_off);

  frame_out->hdr.seq = elem_hdr->seq;
  frame_out->data.ptr = (uint8_t*)elem_hdr + sizeof(a0_fcl_elem_hdr_t);
  frame_out->data.size = elem_hdr->data_size;
  return A0_OK;
}

A0_STATIC_INLINE
fcl_off_t a0_fcl_elem_end(a0_fcl_hdr_t* hdr, fcl_off_t elem_off) {
  a0_fcl_elem_hdr_t* elem_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + elem_off);
  return elem_off + sizeof(a0_fcl_elem_hdr_t) + elem_hdr->data_size;
}

A0_STATIC_INLINE
bool a0_fcl_intersects(fcl_off_t elem1_start,
                       size_t elem1_size,
                       fcl_off_t elem2_start,
                       size_t elem2_size) {
  fcl_off_t elem1_end = elem1_start + elem1_size;
  fcl_off_t elem2_end = elem2_start + elem2_size;
  return (elem1_start <= elem2_end) && (elem2_start <= elem1_end);
}

A0_STATIC_INLINE
bool a0_fcl_head_interval(a0_locked_stream_t lk,
                          fcl_off_t* head_off,
                          size_t* head_size) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;

  bool empty;
  a0_stream_empty(lk, &empty);
  if (empty) {
    return false;
  }

  *head_off = a0_fcl_working_page(lk)->off_head;
  a0_fcl_elem_hdr_t* head_hdr = (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + *head_off);
  *head_size = sizeof(a0_fcl_elem_hdr_t) + head_hdr->data_size;
  return true;
}

A0_STATIC_INLINE
void a0_fcl_remove_head(a0_locked_stream_t lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  a0_fcl_state_t* state = a0_fcl_working_page(lk);

  a0_fcl_elem_hdr_t* head_hdr =
      (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + state->off_head);

  state->seq_low++;
  if (state->off_head == state->off_tail) {
    state->off_head = 0;
    state->off_tail = 0;
  } else {
    state->off_head = head_hdr->next_off;
  }
  a0_stream_commit(lk);
}

errno_t a0_stream_alloc(a0_locked_stream_t lk,
                        size_t size,
                        a0_stream_frame_t* frame_out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;

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
  elem_hdr->data_size = size;

  if (state->off_tail) {
    a0_fcl_elem_hdr_t* tail_elem_hdr =
        (a0_fcl_elem_hdr_t*)((uint8_t*)hdr + state->off_tail);
    tail_elem_hdr->next_off = off;
  }
  state->off_tail = off;
  if (!state->seq_low) {
    state->seq_low = elem_hdr->seq;
  }

  frame_out->hdr.seq = elem_hdr->seq;
  frame_out->data.ptr = (uint8_t*)hdr + off + sizeof(a0_fcl_elem_hdr_t);
  frame_out->data.size = size;

  return A0_OK;
}

errno_t a0_stream_commit(a0_locked_stream_t lk) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;
  hdr->committed_page_idx = !hdr->committed_page_idx;
  *a0_fcl_working_page(lk) = *a0_fcl_committed_page(lk);

  hdr->fu_cond_var++;
  a0_futex_notify_change(&hdr->fu_cond_var);

  return A0_OK;
}

void a0_stream_debugstr(a0_locked_stream_t lk, a0_buf_t* out) {
  a0_fcl_hdr_t* hdr = (a0_fcl_hdr_t*)lk.stream->_shmobj.ptr;

  a0_fcl_state_t* committed_state = a0_fcl_committed_page(lk);
  a0_fcl_state_t* working_state = a0_fcl_working_page(lk);

  FILE* ss = open_memstream((char**)&out->ptr, &out->size);
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
  fprintf(ss, "-- name          = '%.*s'\n", (int)hdr->protocol_name_size, (char*)hdr + a0_fcl_protocol_name_off(hdr));
  fprintf(ss, "-- semver        = %d.%d.%d\n", hdr->protocol_major_version, hdr->protocol_minor_version, hdr->protocol_patch_version);
  fprintf(ss, "-- metadata size = %lu\n", hdr->protocol_metadata_size);
  if (hdr->protocol_metadata_size <= 32) {
    fprintf(ss, "-- metadata      = '%.*s'\n", (int)hdr->protocol_metadata_size, (char*)hdr + a0_fcl_protocol_metadata_off(hdr));
  } else {
    fprintf(ss, "-- metadata      = '%.*s...'\n", 29, (char*)hdr + a0_fcl_protocol_metadata_off(hdr));
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
      fprintf(ss, "-- size   = %lu\n", elem_hdr->data_size);
      if (elem_hdr->data_size <= 32) {
        fprintf(ss, "-- data   = '%.*s'\n", (int)elem_hdr->data_size, (char*)hdr + elem_hdr->off + sizeof(a0_fcl_elem_hdr_t));
      } else {
        fprintf(ss, "-- data   = '%.*s...'\n", 29, (char*)hdr + elem_hdr->off + sizeof(a0_fcl_elem_hdr_t));
      }

      off = elem_hdr->next_off;
      seq++;
    }
  }
  fprintf(ss, "=========================\n");
  fflush(ss);
  fclose(ss);
}
