#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/tid.h>
#include <a0/transport.h>
#include <a0/unused.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "err_macro.h"

#ifdef DEBUG
#include "assert.h"
#include "ref_cnt.h"
#endif

// Synchronous zero-copy version.

errno_t a0_reader_sync_zc_init(a0_reader_sync_zc_t* reader_sync_zc,
                               a0_arena_t arena,
                               a0_reader_init_t init,
                               a0_reader_iter_t iter) {
  reader_sync_zc->_init = init;
  reader_sync_zc->_iter = iter;
  reader_sync_zc->_first_read_done = false;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&reader_sync_zc->_transport, arena));

  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  if (init == A0_INIT_OLDEST) {
    a0_transport_jump_head(tlk);
  } else if (init == A0_INIT_MOST_RECENT || init == A0_INIT_AWAIT_NEW) {
    a0_transport_jump_tail(tlk);
  }

  A0_RETURN_ERR_ON_ERR(a0_transport_unlock(tlk));

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(arena.buf.ptr, NULL), "");
#endif

  return A0_OK;
}

errno_t a0_reader_sync_zc_close(a0_reader_sync_zc_t* reader_sync_zc) {
  A0_MAYBE_UNUSED(reader_sync_zc);
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot close null reader (sync+zc).");

  A0_ASSERT_OK(
      a0_ref_cnt_dec(reader_sync_zc->_transport._arena.buf.ptr, NULL),
      "Reader (sync+zc) closing. Arena was previously closed.");
#endif

  return A0_OK;
}

errno_t a0_reader_sync_zc_has_next(a0_reader_sync_zc_t* reader_sync_zc, bool* has_next) {
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");
#endif

  errno_t err;
  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  if (reader_sync_zc->_first_read_done || reader_sync_zc->_init == A0_INIT_AWAIT_NEW) {
    err = a0_transport_has_next(tlk, has_next);
  } else {
    err = a0_transport_nonempty(tlk, has_next);
  }

  a0_transport_unlock(tlk);
  return err;
}

errno_t a0_reader_sync_zc_next(a0_reader_sync_zc_t* reader_sync_zc,
                               a0_zero_copy_callback_t cb) {
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");
#endif

  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_transport, &tlk));

  bool empty;
  a0_transport_empty(tlk, &empty);
  if (empty) {
    a0_transport_unlock(tlk);
    return EAGAIN;
  }

#ifdef DEBUG
  bool has_next = true;
  if (reader_sync_zc->_first_read_done || reader_sync_zc->_init == A0_INIT_AWAIT_NEW) {
    a0_transport_has_next(tlk, &has_next);
  }
  if (!has_next) {
    a0_transport_unlock(tlk);
    return EAGAIN;
  }
#endif

  bool should_step = reader_sync_zc->_first_read_done || reader_sync_zc->_init == A0_INIT_AWAIT_NEW;
  if (!should_step) {
    bool is_valid;
    a0_transport_ptr_valid(tlk, &is_valid);
    should_step = !is_valid;
  }

  if (should_step) {
    if (reader_sync_zc->_iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_sync_zc->_iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }
  }
  reader_sync_zc->_first_read_done = true;

  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t flat_packet = {
      .buf = {
          .ptr = frame.data,
          .size = frame.hdr.data_size,
      },
  };

  cb.fn(cb.user_data, tlk, flat_packet);

  return a0_transport_unlock(tlk);
}

// Synchronous version.

errno_t a0_reader_sync_init(a0_reader_sync_t* reader_sync,
                            a0_arena_t arena,
                            a0_alloc_t alloc,
                            a0_reader_init_t init,
                            a0_reader_iter_t iter) {
  reader_sync->_alloc = alloc;
  return a0_reader_sync_zc_init(&reader_sync->_reader_sync_zc, arena, init, iter);
}

errno_t a0_reader_sync_close(a0_reader_sync_t* reader_sync) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot close from null reader (sync).");
#endif

  return a0_reader_sync_zc_close(&reader_sync->_reader_sync_zc);
}

errno_t a0_reader_sync_has_next(a0_reader_sync_t* reader_sync, bool* has_next) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");
#endif

  return a0_reader_sync_zc_has_next(&reader_sync->_reader_sync_zc, has_next);
}

typedef struct a0_reader_sync_next_data_s {
  a0_alloc_t alloc;
  a0_packet_t* out_pkt;
} a0_reader_sync_next_data_t;

A0_STATIC_INLINE
void a0_reader_sync_next_impl(void* user_data, a0_transport_locked_t tlk, a0_flat_packet_t fpkt) {
  A0_MAYBE_UNUSED(tlk);
  a0_reader_sync_next_data_t* data = (a0_reader_sync_next_data_t*)user_data;
  a0_buf_t unused;
  a0_packet_deserialize(fpkt, data->alloc, data->out_pkt, &unused);
}

errno_t a0_reader_sync_next(a0_reader_sync_t* reader_sync, a0_packet_t* pkt) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");
#endif

  a0_reader_sync_next_data_t data = (a0_reader_sync_next_data_t){
      .alloc = reader_sync->_alloc,
      .out_pkt = pkt,
  };
  a0_zero_copy_callback_t zc_cb = (a0_zero_copy_callback_t){
      .user_data = &data,
      .fn = a0_reader_sync_next_impl,
  };
  return a0_reader_sync_zc_next(&reader_sync->_reader_sync_zc, zc_cb);
}

// Threaded zero-copy version.

A0_STATIC_INLINE
void a0_reader_zc_thread_handle_pkt(a0_reader_zc_t* reader_zc, a0_transport_locked_t tlk) {
  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t fpkt = {
      .buf = {
          .ptr = frame.data,
          .size = frame.hdr.data_size,
      },
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
      a0_transport_ptr_valid(tlk, &ptr_valid);
      reset = !ptr_valid;
    }

    if (reset) {
      a0_transport_jump_head(tlk);
    }

    if (reset || reader_zc->_init == A0_INIT_OLDEST || reader_zc->_init == A0_INIT_MOST_RECENT) {
      a0_reader_zc_thread_handle_pkt(reader_zc, tlk);
    }

    return true;
  }

  return false;
}

A0_STATIC_INLINE
bool a0_reader_zc_thread_handle_next_pkt(a0_reader_zc_t* reader_zc, a0_transport_locked_t tlk) {
  if (a0_transport_wait(tlk, a0_transport_has_next_pred(&tlk)) == A0_OK) {
    if (reader_zc->_iter == A0_ITER_NEXT) {
      a0_transport_step_next(tlk);
    } else if (reader_zc->_iter == A0_ITER_NEWEST) {
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

errno_t a0_reader_zc_init(a0_reader_zc_t* reader_zc,
                          a0_arena_t arena,
                          a0_reader_init_t init,
                          a0_reader_iter_t iter,
                          a0_zero_copy_callback_t onpacket) {
  reader_zc->_init = init;
  reader_zc->_iter = iter;
  reader_zc->_onpacket = onpacket;

  A0_RETURN_ERR_ON_ERR(a0_transport_init(&reader_zc->_transport, arena));

#ifdef DEBUG
  a0_ref_cnt_inc(arena.buf.ptr, NULL);
#endif

  a0_transport_locked_t tlk;
  a0_transport_lock(&reader_zc->_transport, &tlk);

  a0_transport_empty(tlk, &reader_zc->_started_empty);
  if (!reader_zc->_started_empty) {
    if (init == A0_INIT_OLDEST) {
      a0_transport_jump_head(tlk);
    } else if (init == A0_INIT_MOST_RECENT || init == A0_INIT_AWAIT_NEW) {
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

errno_t a0_reader_zc_close(a0_reader_zc_t* reader_zc) {
  a0_event_wait(&reader_zc->_thread_start_event);
  if (pthread_equal(pthread_self(), reader_zc->_thread_id)) {
    return EDEADLK;
  }
#ifdef DEBUG
  a0_ref_cnt_dec(reader_zc->_transport._arena.buf.ptr, NULL);
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

errno_t a0_reader_init(a0_reader_t* reader,
                       a0_arena_t arena,
                       a0_alloc_t alloc,
                       a0_reader_init_t init,
                       a0_reader_iter_t iter,
                       a0_packet_callback_t onpacket) {
  reader->_alloc = alloc;
  reader->_onpacket = onpacket;

  a0_zero_copy_callback_t onpacket_wrapper = (a0_zero_copy_callback_t){
      .user_data = reader,
      .fn = a0_reader_onpacket_wrapper,
  };

  return a0_reader_zc_init(&reader->_reader_zc, arena, init, iter, onpacket_wrapper);
}

errno_t a0_reader_close(a0_reader_t* reader) {
  return a0_reader_zc_close(&reader->_reader_zc);
}

// Read one version.

typedef struct a0_reader_read_one_data_s {
  a0_packet_t* pkt;
  a0_event_t done_event;
} a0_reader_read_one_data_t;

void a0_reader_read_one_callback(void* user_data, a0_packet_t pkt) {
  a0_reader_read_one_data_t* data = (a0_reader_read_one_data_t*)user_data;
  if (a0_event_is_set(&data->done_event)) {
    return;
  }

  *data->pkt = pkt;
  a0_event_set(&data->done_event);
}

A0_STATIC_INLINE
errno_t a0_reader_read_one_blocking(a0_arena_t arena,
                                    a0_alloc_t alloc,
                                    a0_reader_init_t init,
                                    a0_packet_t* out) {
  a0_reader_read_one_data_t data;
  data.pkt = out;
  a0_event_init(&data.done_event);

  a0_packet_callback_t onpacket = {
      .user_data = &data,
      .fn = a0_reader_read_one_callback,
  };

  a0_reader_t reader;
  errno_t err = a0_reader_init(&reader, arena, alloc, init, A0_ITER_NEXT, onpacket);
  if (err) {
    a0_event_close(&data.done_event);
    return err;
  }

  a0_event_wait(&data.done_event);

  a0_reader_close(&reader);
  a0_event_close(&data.done_event);

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_reader_read_one_nonblocking(a0_arena_t arena,
                                       a0_alloc_t alloc,
                                       a0_reader_init_t init,
                                       a0_packet_t* out) {
  if (init == A0_INIT_AWAIT_NEW) {
    return EAGAIN;
  }

  a0_reader_sync_t reader_sync;
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, arena, alloc, init, A0_ITER_NEXT));

  bool has_next;
  errno_t err = a0_reader_sync_has_next(&reader_sync, &has_next);
  if (!err) {
    if (has_next) {
      a0_reader_sync_next(&reader_sync, out);
    } else {
      err = EAGAIN;
    }
  }
  a0_reader_sync_close(&reader_sync);
  return err;
}

errno_t a0_reader_read_one(a0_arena_t arena,
                           a0_alloc_t alloc,
                           a0_reader_init_t init,
                           int flags,
                           a0_packet_t* out) {
  if (flags & O_NONBLOCK) {
    return a0_reader_read_one_nonblocking(arena, alloc, init, out);
  }
  return a0_reader_read_one_blocking(arena, alloc, init, out);
}
