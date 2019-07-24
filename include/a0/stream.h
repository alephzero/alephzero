#ifndef A0_STREAM_H
#define A0_STREAM_H

#include <a0/err.h>
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

struct a0_stream_options_s {
  a0_shmobj_t* shmobj;
  a0_stream_construct_options_t* construct_opts;
};

struct a0_stream_s {
  void* user_data;

  a0_stream_options_t _opts;
  uint64_t _seq;
  uint64_t _off;
};

typedef struct a0_stream_elem_hdr_s {
  uint64_t seq;
} a0_stream_elem_hdr_t;

errno_t a0_stream_init(a0_stream_t*, const a0_stream_options_t*);
errno_t a0_stream_close(a0_stream_t*);

typedef struct a0_locked_stream_s {
  a0_stream_t* stream;
} a0_locked_stream_t;

errno_t a0_lock_stream(a0_locked_stream_t*, a0_stream_t*);
errno_t a0_unlock_stream(a0_locked_stream_t*);

// Caller does NOT own `out->base` and should not clean it up!
errno_t a0_stream_protocol_metadata(a0_locked_stream_t*, a0_buf_t* out);

errno_t a0_stream_is_empty(a0_locked_stream_t*, bool*);
errno_t a0_stream_await_nonempty(a0_locked_stream_t*);
errno_t a0_stream_jump_head(a0_locked_stream_t*);
errno_t a0_stream_jump_tail(a0_locked_stream_t*);  // Inclusive.
errno_t a0_stream_has_next(a0_locked_stream_t*, bool*);
errno_t a0_stream_next(a0_locked_stream_t*);
errno_t a0_stream_await_next(a0_locked_stream_t*);

// Caller does NOT own `payload_out->base` and should not clean it up!
errno_t a0_stream_elem(a0_locked_stream_t*, a0_stream_elem_hdr_t* hdr_out, a0_buf_t* payload_out);

// Caller does NOT own `out` and should not clean it up!
//
// For robustness, allocated elements are not tracked until explicitly commited.
// Note: if an alloc evicts an old element, that element is lost, even if no commit call is issued.
errno_t a0_stream_alloc(a0_locked_stream_t*, size_t, a0_buf_t* out);
errno_t a0_stream_commit(a0_locked_stream_t*);

void _a0_testing_stream_debugstr(a0_locked_stream_t* lk, char** out_str, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif  // A0_STREAM_H
