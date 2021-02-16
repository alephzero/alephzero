#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/transport.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <syscall.h>
#include <unistd.h>

#include "err_util.h"
#include "event.h"
#include "inline.h"
#include "unused.h"

#ifdef DEBUG
#include "assert.h"
#include "ref_cnt.h"
#endif

// Synchronous zero-copy version.

struct a0_reader_sync_zc_impl_s {
  a0_transport_t transport;
  a0_reader_iter_t iter;
  bool init_handled;
};

errno_t a0_reader_sync_zc_init(a0_reader_sync_zc_t* reader_sync_zc,
                               a0_arena_t arena,
                               a0_reader_init_t init,
                               a0_reader_iter_t iter) {
  a0_transport_t transport;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&transport, arena));

  a0_locked_transport_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&transport, &tlk));

  if (init == A0_INIT_OLDEST) {
    a0_transport_jump_head(tlk);
  } else if (init == A0_INIT_MOST_RECENT || init == A0_INIT_AWAIT_NEW) {
    a0_transport_jump_tail(tlk);
  }

  A0_RETURN_ERR_ON_ERR(a0_transport_unlock(tlk));

  reader_sync_zc->_impl = (a0_reader_sync_zc_impl_t*)malloc(sizeof(a0_reader_sync_zc_impl_t));
  *reader_sync_zc->_impl = (a0_reader_sync_zc_impl_t){
      .transport = transport,
      .iter = iter,
      .init_handled = false,
  };

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(arena.buf.ptr, NULL), "");
#endif

  return A0_OK;
}

errno_t a0_reader_sync_zc_close(a0_reader_sync_zc_t* reader_sync_zc) {
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot close null reader (sync+zc).");
  A0_ASSERT(reader_sync_zc->_impl, "Cannot close uninitialized/closed reader (sync+zc).");

  A0_ASSERT_OK(
      a0_ref_cnt_dec(reader_sync_zc->_impl->transport._arena.buf.ptr, NULL),
      "Reader (sync+zc) closing. Arena was previously closed.");
#endif

  free(reader_sync_zc->_impl);
  reader_sync_zc->_impl = NULL;

  return A0_OK;
}

errno_t a0_reader_sync_zc_has_next(a0_reader_sync_zc_t* reader_sync_zc, bool* has_next) {
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");
  A0_ASSERT(reader_sync_zc->_impl, "Cannot read from uninitialized/closed reader (sync+zc).");
#endif

  a0_locked_transport_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_impl->transport, &tlk));
  errno_t err = a0_transport_has_next(tlk, has_next);
  a0_transport_unlock(tlk);
  return err;
}

errno_t a0_reader_sync_zc_next(a0_reader_sync_zc_t* reader_sync_zc,
                               a0_zero_copy_callback_t cb) {
#ifdef DEBUG
  A0_ASSERT(reader_sync_zc, "Cannot read from null reader (sync+zc).");
  A0_ASSERT(reader_sync_zc->_impl, "Cannot read from uninitialized/closed reader (sync+zc).");
#endif

  a0_locked_transport_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(&reader_sync_zc->_impl->transport, &tlk));

  bool is_valid;
  a0_transport_ptr_valid(tlk, &is_valid);

  if (reader_sync_zc->_impl->init_handled || !is_valid) {
    bool has_next;
    a0_transport_has_next(tlk, &has_next);
    if (!has_next) {
      a0_transport_unlock(tlk);
      return EAGAIN;
    }

    if (reader_sync_zc->_impl->iter == A0_ITER_NEXT) {
      a0_transport_next(tlk);
    } else if (reader_sync_zc->_impl->iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }
  }
  reader_sync_zc->_impl->init_handled = true;

  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t flat_packet = (a0_buf_t){
      .ptr = frame.data,
      .size = frame.hdr.data_size,
  };

  cb.fn(cb.user_data, tlk, flat_packet);

  return a0_transport_unlock(tlk);
}

// Synchronous version.

struct a0_reader_sync_impl_s {
  a0_reader_sync_zc_t reader_sync_zc;
  a0_alloc_t alloc;
};

errno_t a0_reader_sync_init(a0_reader_sync_t* reader_sync,
                            a0_arena_t arena,
                            a0_alloc_t alloc,
                            a0_reader_init_t init,
                            a0_reader_iter_t iter) {
  reader_sync->_impl = (a0_reader_sync_impl_t*)malloc(sizeof(a0_reader_sync_impl_t));
  reader_sync->_impl->alloc = alloc;
  errno_t err = a0_reader_sync_zc_init(&reader_sync->_impl->reader_sync_zc, arena, init, iter);
  if (err) {
    free(reader_sync->_impl);
    reader_sync->_impl = NULL;
  }
  return err;
}

errno_t a0_reader_sync_close(a0_reader_sync_t* reader_sync) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot close from null reader (sync).");
  A0_ASSERT(reader_sync->_impl, "Cannot close from uninitialized/closed reader (sync).");
#endif

  a0_reader_sync_zc_close(&reader_sync->_impl->reader_sync_zc);
  free(reader_sync->_impl);
  reader_sync->_impl = NULL;
  return A0_OK;
}

errno_t a0_reader_sync_has_next(a0_reader_sync_t* reader_sync, bool* has_next) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");
  A0_ASSERT(reader_sync->_impl, "Cannot read from uninitialized/closed reader (sync).");
#endif

  return a0_reader_sync_zc_has_next(&reader_sync->_impl->reader_sync_zc, has_next);
}

typedef struct a0_reader_sync_next_data_s {
  a0_alloc_t alloc;
  a0_packet_t* out_pkt;
} a0_reader_sync_next_data_t;

A0_STATIC_INLINE
void a0_reader_sync_next_impl(void* user_data, a0_locked_transport_t tlk, a0_flat_packet_t fpkt) {
  A0_MAYBE_UNUSED(tlk);
  a0_reader_sync_next_data_t* data = (a0_reader_sync_next_data_t*)user_data;
  a0_packet_deep_deserialize(fpkt, data->alloc, data->out_pkt);
}

errno_t a0_reader_sync_next(a0_reader_sync_t* reader_sync, a0_packet_t* pkt) {
#ifdef DEBUG
  A0_ASSERT(reader_sync, "Cannot read from null reader (sync).");
  A0_ASSERT(reader_sync->_impl, "Cannot read from uninitialized/closed reader (sync).");
#endif

  a0_reader_sync_next_data_t data = (a0_reader_sync_next_data_t){
      .alloc = reader_sync->_impl->alloc,
      .out_pkt = pkt,
  };
  a0_zero_copy_callback_t zc_cb = (a0_zero_copy_callback_t){
      .user_data = &data,
      .fn = a0_reader_sync_next_impl,
  };
  return a0_reader_sync_zc_next(&reader_sync->_impl->reader_sync_zc, zc_cb);
}

// Threaded zero-copy version.

struct a0_reader_zc_impl_s {
  a0_transport_t transport;
  bool started_empty;

  pthread_t thread;
  uint32_t thread_id;
  a0_event_t thread_start_event;

  a0_reader_init_t init;
  a0_reader_iter_t iter;

  a0_zero_copy_callback_t onpacket;
  a0_callback_t onclose;
};

A0_STATIC_INLINE
void a0_reader_zc_thread_handle_pkt(a0_reader_zc_impl_t* impl, a0_locked_transport_t tlk) {
  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t fpkt = (a0_buf_t){
      .ptr = frame.data,
      .size = frame.hdr.data_size,
  };

  impl->onpacket.fn(impl->onpacket.user_data, tlk, fpkt);
}

A0_STATIC_INLINE
bool a0_reader_zc_thread_handle_first_pkt(a0_reader_zc_impl_t* impl, a0_locked_transport_t tlk) {
  if (a0_transport_await(tlk, a0_transport_nonempty) == A0_OK) {
    bool reset = false;
    if (impl->started_empty) {
      reset = true;
    } else {
      bool ptr_valid;
      a0_transport_ptr_valid(tlk, &ptr_valid);
      reset = !ptr_valid;
    }

    if (reset) {
      a0_transport_jump_head(tlk);
    }

    if (reset || impl->init == A0_INIT_OLDEST || impl->init == A0_INIT_MOST_RECENT) {
      a0_reader_zc_thread_handle_pkt(impl, tlk);
    }

    return true;
  }

  return false;
}

A0_STATIC_INLINE
bool a0_reader_zc_thread_handle_next_pkt(a0_reader_zc_impl_t* impl, a0_locked_transport_t tlk) {
  if (a0_transport_await(tlk, a0_transport_has_next) == A0_OK) {
    if (impl->iter == A0_ITER_NEXT) {
      a0_transport_next(tlk);
    } else if (impl->iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }

    a0_reader_zc_thread_handle_pkt(impl, tlk);

    return true;
  }

  return false;
}

A0_STATIC_INLINE
void* a0_reader_zc_thread_main(void* data) {
  a0_reader_zc_impl_t* impl = (a0_reader_zc_impl_t*)data;
  // Alert that the thread has started.
  impl->thread_id = syscall(SYS_gettid);
  a0_event_set(&impl->thread_start_event);

  // Lock until shutdown.
  // Lock will release lock while awaiting packets.
  a0_locked_transport_t tlk;
  a0_transport_lock(&impl->transport, &tlk);

  // Loop until shutdown is triggered.
  if (a0_reader_zc_thread_handle_first_pkt(impl, tlk)) {
    while (a0_reader_zc_thread_handle_next_pkt(impl, tlk)) {
    }
  }

  // Start shutdown.

  // No longer need transport access.
  a0_transport_unlock(tlk);

  // Be nice and cleanup memory.
  a0_event_close(&impl->thread_start_event);

  // Save for later.
  a0_callback_t onclose = impl->onclose;

  // Done with impl.
  free(impl);

  // Alert user that shutdown is complete.
  onclose.fn(onclose.user_data);

  return NULL;
}

errno_t a0_reader_zc_init(a0_reader_zc_t* reader_zc,
                          a0_arena_t arena,
                          a0_reader_init_t init,
                          a0_reader_iter_t iter,
                          a0_zero_copy_callback_t onpacket) {
  a0_reader_zc_impl_t* impl = (a0_reader_zc_impl_t*)malloc(sizeof(a0_reader_zc_impl_t));

  errno_t err = a0_transport_init(&impl->transport, arena);
  if (err) {
    free(impl);
    return err;
  }

#ifdef DEBUG
  a0_ref_cnt_inc(arena.buf.ptr, NULL);
#endif

  a0_locked_transport_t tlk;
  a0_transport_lock(&impl->transport, &tlk);

  a0_transport_empty(tlk, &impl->started_empty);
  if (!impl->started_empty) {
    if (init == A0_INIT_OLDEST) {
      a0_transport_jump_head(tlk);
    } else if (init == A0_INIT_MOST_RECENT || init == A0_INIT_AWAIT_NEW) {
      a0_transport_jump_tail(tlk);
    }
  }

  a0_transport_unlock(tlk);

  impl->init = init;
  impl->iter = iter;
  impl->onpacket = onpacket;
  impl->onclose.user_data = NULL;
  impl->onclose.fn = NULL;
  a0_event_init(&impl->thread_start_event);

  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

  pthread_create(
      &impl->thread,
      &thread_attr,
      a0_reader_zc_thread_main,
      impl);

  reader_zc->_impl = impl;

  return A0_OK;
}

errno_t a0_reader_zc_async_close(a0_reader_zc_t* reader_zc, a0_locked_transport_t tlk, a0_callback_t onclose) {
#ifdef DEBUG
  a0_ref_cnt_dec(reader_zc->_impl->transport._arena.buf.ptr, NULL);
#endif

  reader_zc->_impl->onclose = onclose;
  return a0_transport_shutdown(tlk);
}

errno_t a0_reader_zc_close(a0_reader_zc_t* reader_zc) {
  a0_event_wait(&reader_zc->_impl->thread_start_event);
  if (syscall(SYS_gettid) == reader_zc->_impl->thread_id) {
    return EDEADLK;
  }

  a0_locked_transport_t tlk;
  a0_transport_lock(&reader_zc->_impl->transport, &tlk);

  a0_event_t close_event;
  a0_event_init(&close_event);

  a0_callback_t onclose = (a0_callback_t){
      .user_data = &close_event,
      .fn = (void (*)(void*))a0_event_set,
  };

  a0_reader_zc_async_close(reader_zc, tlk, onclose);

  a0_event_wait(&close_event);
  a0_event_close(&close_event);

  return A0_OK;
}

// Threaded version.

struct a0_reader_impl_s {
  a0_reader_zc_t reader_zc;
  a0_alloc_t alloc;
  a0_packet_callback_t onpacket;
  a0_callback_t onclose;
};

A0_STATIC_INLINE
void a0_reader_onpacket_wrapper(void* user_data, a0_locked_transport_t tlk, a0_flat_packet_t fpkt) {
  a0_reader_impl_t* impl = (a0_reader_impl_t*)user_data;
  a0_packet_t pkt;
  a0_packet_deep_deserialize(fpkt, impl->alloc, &pkt);
  a0_transport_unlock(tlk);

  impl->onpacket.fn(impl->onpacket.user_data, pkt);
  a0_packet_dealloc(pkt, impl->alloc);

  a0_transport_lock(tlk.transport, &tlk);
}

errno_t a0_reader_init(a0_reader_t* reader,
                       a0_arena_t arena,
                       a0_alloc_t alloc,
                       a0_reader_init_t init,
                       a0_reader_iter_t iter,
                       a0_packet_callback_t onpacket) {
  reader->_impl = (a0_reader_impl_t*)malloc(sizeof(a0_reader_impl_t));
  reader->_impl->alloc = alloc;
  reader->_impl->onpacket = onpacket;
  a0_zero_copy_callback_t onpacket_wrapper;
  onpacket_wrapper.user_data = &reader->_impl;
  onpacket_wrapper.fn = a0_reader_onpacket_wrapper;
  errno_t err = a0_reader_zc_init(&reader->_impl->reader_zc, arena, init, iter, onpacket_wrapper);
  if (err) {
    free(reader->_impl);
    reader->_impl = NULL;
  }
  return err;
}

A0_STATIC_INLINE
void a0_reader_onclose_wrapper(void* user_data) {
  a0_reader_impl_t* impl = (a0_reader_impl_t*)user_data;
  impl->onclose.fn(impl->onclose.user_data);
  free(impl);
}

errno_t a0_reader_async_close(a0_reader_t* reader, a0_callback_t onclose) {
  reader->_impl->onclose = onclose;

  a0_callback_t onclose_wrapper = (a0_callback_t){
      .user_data = reader->_impl,
      .fn = a0_reader_onclose_wrapper,
  };

  a0_locked_transport_t tlk;
  a0_transport_lock(&reader->_impl->reader_zc._impl->transport, &tlk);
  errno_t err = a0_reader_zc_async_close(&reader->_impl->reader_zc, tlk, onclose_wrapper);
  a0_transport_unlock(tlk);
  return err;
}

errno_t a0_reader_close(a0_reader_t* reader) {
  errno_t err = a0_reader_zc_close(&reader->_impl->reader_zc);
  free(reader->_impl);
  return err;
}

typedef struct a0_reader_read_one_data_s {
  a0_packet_t* pkt;
  a0_event_t done_event;
} a0_reader_read_one_data_t;

void a0_reader_read_one_callback(void* user_data, a0_packet_t pkt) {
  a0_reader_read_one_data_t* data = (a0_reader_read_one_data_t*)user_data;
  if (data->done_event.is_set) {
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
  a0_reader_sync_has_next(&reader_sync, &has_next);
  if (!has_next) {
    return EAGAIN;
  }
  a0_reader_sync_next(&reader_sync, out);
  return a0_reader_sync_close(&reader_sync);
}

errno_t a0_reader_read_one(a0_arena_t arena,
                           a0_alloc_t alloc,
                           a0_reader_init_t init,
                           int flags,
                           a0_packet_t* out) {
  if (flags & O_NDELAY || flags & O_NONBLOCK) {
    return a0_reader_read_one_nonblocking(arena, alloc, init, out);
  }
  return a0_reader_read_one_blocking(arena, alloc, init, out);
}