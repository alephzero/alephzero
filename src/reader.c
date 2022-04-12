#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/tid.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/unused.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "err_macro.h"

#ifdef DEBUG
#include "ref_cnt.h"
#endif

const a0_reader_options_t A0_READER_OPTIONS_DEFAULT = {
    .init = A0_INIT_AWAIT_NEW,
    .iter = A0_ITER_NEXT,
};

// Synchronous zero-copy version.

a0_err_t a0_reader_sync_zc_init(a0_reader_sync_zc_t* reader_sync_zc,
                                a0_arena_t arena,
                                a0_reader_options_t opts) {
  reader_sync_zc->_opts = opts;
  reader_sync_zc->_first_read_done = false;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&reader_sync_zc->_transport, arena));

  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  if (opts.init == A0_INIT_OLDEST) {
    a0_transport_jump_head(tlk);
  } else if (opts.init == A0_INIT_MOST_RECENT || opts.init == A0_INIT_AWAIT_NEW) {
    a0_transport_jump_tail(tlk);
  }

  A0_RETURN_ERR_ON_ERR(a0_transport_unlock(tlk));

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(arena.buf.data, NULL), "");
#endif

  return A0_OK;
}

a0_err_t a0_reader_sync_zc_close(a0_reader_sync_zc_t* reader_sync_zc) {
  A0_ASSERT(reader_sync_zc, "Cannot close null reader (sync+zc).");

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(reader_sync_zc->_transport._arena.buf.data, NULL),
      "Reader (sync+zc) closing. Arena was previously closed.");
#endif

  return A0_OK;
}

a0_err_t a0_reader_sync_zc_can_read(a0_reader_sync_zc_t* reader_sync_zc, bool* can_read) {
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");

  a0_err_t err;
  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  if (reader_sync_zc->_first_read_done || reader_sync_zc->_opts.init == A0_INIT_AWAIT_NEW) {
    err = a0_transport_has_next(tlk, can_read);
  } else {
    err = a0_transport_nonempty(tlk, can_read);
  }

  a0_transport_unlock(tlk);
  return err;
}

typedef struct a0_reader_sync_zc_align_read_callback_s {
  void* user_data;
  a0_err_t (*fn)(void* user_data, a0_reader_sync_zc_t*, a0_transport_locked_t);
} a0_reader_sync_zc_read_align_callback_t;

A0_STATIC_INLINE
a0_err_t a0_reader_sync_zc_read_helper(a0_reader_sync_zc_t* reader_sync_zc,
                                       a0_zero_copy_callback_t cb,
                                       a0_reader_sync_zc_read_align_callback_t align_read) {
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");

  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  a0_err_t err = align_read.fn(align_read.user_data, reader_sync_zc, tlk);
  if (err) {
    a0_transport_unlock(tlk);
    return err;
  }

  reader_sync_zc->_first_read_done = true;

  a0_transport_frame_t* frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t flat_packet = {
      .buf = {frame->data, frame->hdr.data_size},
  };

  cb.fn(cb.user_data, tlk, flat_packet);

  return a0_transport_unlock(tlk);
}

A0_STATIC_INLINE
a0_err_t a0_reader_sync_zc_read_align(void* unused, a0_reader_sync_zc_t* reader_sync_zc, a0_transport_locked_t tlk) {
  A0_MAYBE_UNUSED(unused);
  bool empty;
  a0_transport_empty(tlk, &empty);
  if (empty) {
    return A0_ERR_AGAIN;
  }

  if (reader_sync_zc->_first_read_done || reader_sync_zc->_opts.init == A0_INIT_AWAIT_NEW) {
    bool has_next = true;
    a0_transport_has_next(tlk, &has_next);
    if (!has_next) {
      return A0_ERR_AGAIN;
    }
  }

  bool should_step = reader_sync_zc->_first_read_done || reader_sync_zc->_opts.init == A0_INIT_AWAIT_NEW;
  if (!should_step) {
    bool is_valid;
    a0_transport_iter_valid(tlk, &is_valid);
    should_step = !is_valid;
  }

  if (should_step) {
    if (reader_sync_zc->_opts.iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_sync_zc->_opts.iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }
  }

  return A0_OK;
}

a0_err_t a0_reader_sync_zc_read(a0_reader_sync_zc_t* reader_sync_zc,
                                a0_zero_copy_callback_t cb) {
  return a0_reader_sync_zc_read_helper(
      reader_sync_zc,
      cb,
      (a0_reader_sync_zc_read_align_callback_t){NULL, a0_reader_sync_zc_read_align});
}

A0_STATIC_INLINE
a0_err_t a0_reader_sync_zc_read_blocking_align(void* unused, a0_reader_sync_zc_t* reader_sync_zc, a0_transport_locked_t tlk) {
  A0_MAYBE_UNUSED(unused);
  A0_RETURN_ERR_ON_ERR(a0_transport_wait(tlk, a0_transport_nonempty_pred(&tlk)));

  bool should_step = reader_sync_zc->_first_read_done || reader_sync_zc->_opts.init == A0_INIT_AWAIT_NEW;
  if (!should_step) {
    bool is_valid;
    a0_transport_iter_valid(tlk, &is_valid);
    should_step = !is_valid;
  }

  if (should_step) {
    A0_RETURN_ERR_ON_ERR(a0_transport_wait(tlk, a0_transport_has_next_pred(&tlk)));
    if (reader_sync_zc->_opts.iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_sync_zc->_opts.iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }
  }

  return A0_OK;
}

a0_err_t a0_reader_sync_zc_read_blocking(a0_reader_sync_zc_t* reader_sync_zc,
                                         a0_zero_copy_callback_t cb) {
  return a0_reader_sync_zc_read_helper(
      reader_sync_zc,
      cb,
      (a0_reader_sync_zc_read_align_callback_t){NULL, a0_reader_sync_zc_read_blocking_align});
}

A0_STATIC_INLINE
a0_err_t a0_reader_sync_zc_read_blocking_timeout_align(void* user_data, a0_reader_sync_zc_t* reader_sync_zc, a0_transport_locked_t tlk) {
  a0_time_mono_t* timeout = (a0_time_mono_t*)user_data;
  A0_RETURN_ERR_ON_ERR(a0_transport_timedwait(tlk, a0_transport_nonempty_pred(&tlk), timeout));

  bool should_step = reader_sync_zc->_first_read_done || reader_sync_zc->_opts.init == A0_INIT_AWAIT_NEW;
  if (!should_step) {
    bool is_valid;
    a0_transport_iter_valid(tlk, &is_valid);
    should_step = !is_valid;
  }

  if (should_step) {
    A0_RETURN_ERR_ON_ERR(a0_transport_timedwait(tlk, a0_transport_has_next_pred(&tlk), timeout));
    if (reader_sync_zc->_opts.iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_sync_zc->_opts.iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }
  }

  return A0_OK;
}

a0_err_t a0_reader_sync_zc_read_blocking_timeout(a0_reader_sync_zc_t* reader_sync_zc,
                                                 a0_time_mono_t* timeout,
                                                 a0_zero_copy_callback_t cb) {
  return a0_reader_sync_zc_read_helper(
      reader_sync_zc,
      cb,
      (a0_reader_sync_zc_read_align_callback_t){timeout, a0_reader_sync_zc_read_blocking_timeout_align});
}

// Synchronous version.

a0_err_t a0_reader_sync_init(a0_reader_sync_t* reader_sync,
                             a0_arena_t arena,
                             a0_alloc_t alloc,
                             a0_reader_options_t opts) {
  reader_sync->_alloc = alloc;
  return a0_reader_sync_zc_init(&reader_sync->_reader_sync_zc, arena, opts);
}

a0_err_t a0_reader_sync_close(a0_reader_sync_t* reader_sync) {
  A0_ASSERT(reader_sync, "Cannot close from null reader (sync).");

  return a0_reader_sync_zc_close(&reader_sync->_reader_sync_zc);
}

a0_err_t a0_reader_sync_can_read(a0_reader_sync_t* reader_sync, bool* can_read) {
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");

  return a0_reader_sync_zc_can_read(&reader_sync->_reader_sync_zc, can_read);
}

typedef struct a0_reader_sync_read_data_s {
  a0_alloc_t alloc;
  a0_packet_t* out_pkt;
} a0_reader_sync_read_data_t;

A0_STATIC_INLINE
void a0_reader_sync_read_impl(void* user_data, a0_transport_locked_t tlk, a0_flat_packet_t fpkt) {
  A0_MAYBE_UNUSED(tlk);
  a0_reader_sync_read_data_t* data = (a0_reader_sync_read_data_t*)user_data;
  a0_buf_t unused;
  a0_packet_deserialize(fpkt, data->alloc, data->out_pkt, &unused);
}

a0_err_t a0_reader_sync_read(a0_reader_sync_t* reader_sync, a0_packet_t* pkt) {
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");

  a0_reader_sync_read_data_t data = (a0_reader_sync_read_data_t){
      .alloc = reader_sync->_alloc,
      .out_pkt = pkt,
  };
  a0_zero_copy_callback_t zc_cb = (a0_zero_copy_callback_t){
      .user_data = &data,
      .fn = a0_reader_sync_read_impl,
  };
  return a0_reader_sync_zc_read(&reader_sync->_reader_sync_zc, zc_cb);
}

a0_err_t a0_reader_sync_read_blocking(a0_reader_sync_t* reader_sync, a0_packet_t* pkt) {
  return a0_reader_sync_read_blocking_timeout(reader_sync, NULL, pkt);
}

a0_err_t a0_reader_sync_read_blocking_timeout(a0_reader_sync_t* reader_sync, a0_time_mono_t* timeout, a0_packet_t* pkt) {
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");

  a0_reader_sync_read_data_t data = (a0_reader_sync_read_data_t){
      .alloc = reader_sync->_alloc,
      .out_pkt = pkt,
  };
  a0_zero_copy_callback_t zc_cb = (a0_zero_copy_callback_t){
      .user_data = &data,
      .fn = a0_reader_sync_read_impl,
  };
  return a0_reader_sync_zc_read_blocking_timeout(&reader_sync->_reader_sync_zc, timeout, zc_cb);
}

// Threaded zero-copy version.

A0_STATIC_INLINE
void a0_reader_zc_thread_handle_pkt(a0_reader_zc_t* reader_zc, a0_transport_locked_t tlk) {
  a0_transport_frame_t* frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t fpkt = {
      .buf = {frame->data, frame->hdr.data_size},
  };

  reader_zc->_onpacket.fn(reader_zc->_onpacket.user_data, tlk, fpkt);
}

A0_STATIC_INLINE
bool a0_reader_zc_thread_handle_first_pkt(a0_reader_zc_t* reader_zc, a0_transport_locked_t tlk) {
  if (a0_transport_wait(tlk, a0_transport_nonempty_pred(&tlk)) == A0_OK) {
    bool reset = false;
    if (reader_zc->_started_empty) {
      reset = true;
    } else {
      bool ptr_valid;
      a0_transport_iter_valid(tlk, &ptr_valid);
      reset = !ptr_valid;
    }

    if (reset) {
      a0_transport_jump_head(tlk);
    }

    if (reset || reader_zc->_opts.init == A0_INIT_OLDEST || reader_zc->_opts.init == A0_INIT_MOST_RECENT) {
      a0_reader_zc_thread_handle_pkt(reader_zc, tlk);
    }

    return true;
  }

  return false;
}

A0_STATIC_INLINE
bool a0_reader_zc_thread_handle_next_pkt(a0_reader_zc_t* reader_zc, a0_transport_locked_t tlk) {
  if (a0_transport_wait(tlk, a0_transport_has_next_pred(&tlk)) == A0_OK) {
    if (reader_zc->_opts.iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_zc->_opts.iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }

    a0_reader_zc_thread_handle_pkt(reader_zc, tlk);

    return true;
  }

  return false;
}

A0_STATIC_INLINE
void* a0_reader_zc_thread_main(void* data) {
  a0_reader_zc_t* reader_zc = (a0_reader_zc_t*)data;
  // Alert that the thread has started.
  reader_zc->_thread_id = a0_tid();
  a0_event_set(&reader_zc->_thread_start_event);

  // Lock until shutdown.
  // Lock will release lock while awaiting packets.
  a0_transport_locked_t tlk;
  a0_transport_lock(&reader_zc->_transport, &tlk);

  // Loop until shutdown is triggered.
  if (a0_reader_zc_thread_handle_first_pkt(reader_zc, tlk)) {
    while (a0_reader_zc_thread_handle_next_pkt(reader_zc, tlk)) {
    }
  }

  // Shutting down.
  a0_transport_unlock(tlk);

  return NULL;
}

a0_err_t a0_reader_zc_init(a0_reader_zc_t* reader_zc,
                           a0_arena_t arena,
                           a0_reader_options_t opts,
                           a0_zero_copy_callback_t onpacket) {
  reader_zc->_opts = opts;
  reader_zc->_onpacket = onpacket;

  A0_RETURN_ERR_ON_ERR(a0_transport_init(&reader_zc->_transport, arena));

#ifdef DEBUG
  a0_ref_cnt_inc(arena.buf.data, NULL);
#endif

  a0_transport_locked_t tlk;
  a0_transport_lock(&reader_zc->_transport, &tlk);

  a0_transport_empty(tlk, &reader_zc->_started_empty);
  if (!reader_zc->_started_empty) {
    if (opts.init == A0_INIT_OLDEST) {
      a0_transport_jump_head(tlk);
    } else if (opts.init == A0_INIT_MOST_RECENT || opts.init == A0_INIT_AWAIT_NEW) {
      a0_transport_jump_tail(tlk);
    }
  }

  a0_transport_unlock(tlk);

  a0_event_init(&reader_zc->_thread_start_event);

  pthread_create(
      &reader_zc->_thread,
      NULL,
      a0_reader_zc_thread_main,
      reader_zc);

  return A0_OK;
}

a0_err_t a0_reader_zc_close(a0_reader_zc_t* reader_zc) {
  a0_event_wait(&reader_zc->_thread_start_event);
  if (pthread_equal(pthread_self(), reader_zc->_thread_id)) {
    return A0_MAKE_SYSERR(EDEADLK);
  }
#ifdef DEBUG
  a0_ref_cnt_dec(reader_zc->_transport._arena.buf.data, NULL);
#endif

  a0_transport_locked_t tlk;
  a0_transport_lock(&reader_zc->_transport, &tlk);
  a0_transport_shutdown(tlk);
  a0_transport_unlock(tlk);

  a0_event_close(&reader_zc->_thread_start_event);
  pthread_join(reader_zc->_thread, NULL);

  return A0_OK;
}

// Threaded version.

A0_STATIC_INLINE
void a0_reader_onpacket_wrapper(void* user_data, a0_transport_locked_t tlk, a0_flat_packet_t fpkt) {
  a0_reader_t* reader = (a0_reader_t*)user_data;
  a0_packet_t pkt;
  a0_buf_t buf;
  a0_packet_deserialize(fpkt, reader->_alloc, &pkt, &buf);
  a0_transport_unlock(tlk);

  a0_packet_callback_call(reader->_onpacket, pkt);
  a0_dealloc(reader->_alloc, buf);

  a0_transport_lock(tlk.transport, &tlk);
}

a0_err_t a0_reader_init(a0_reader_t* reader,
                        a0_arena_t arena,
                        a0_alloc_t alloc,
                        a0_reader_options_t opts,
                        a0_packet_callback_t onpacket) {
  reader->_alloc = alloc;
  reader->_onpacket = onpacket;

  a0_zero_copy_callback_t onpacket_wrapper = (a0_zero_copy_callback_t){
      .user_data = reader,
      .fn = a0_reader_onpacket_wrapper,
  };

  return a0_reader_zc_init(&reader->_reader_zc, arena, opts, onpacket_wrapper);
}

a0_err_t a0_reader_close(a0_reader_t* reader) {
  return a0_reader_zc_close(&reader->_reader_zc);
}

a0_err_t a0_read_random_access(a0_arena_t arena, size_t off, a0_zero_copy_callback_t cb) {
  a0_transport_t transport;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&transport, arena));

  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&transport, &tlk));

  a0_err_t err = a0_transport_jump(tlk, off);
  if (err) {
    a0_transport_unlock(tlk);
    return err;
  }
  a0_transport_frame_t* frame;
  a0_transport_frame(tlk, &frame);

  cb.fn(cb.user_data, tlk, (a0_flat_packet_t){{frame->data, frame->hdr.data_size}});

  return a0_transport_unlock(tlk);
}
