#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/uuid.h>

#include <fcntl.h>

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string_view>

#include "alloc_util.hpp"
#include "charconv.hpp"
#include "macros.h"
#include "scope.hpp"
#include "sync.hpp"
#include "transport_tools.hpp"

#ifdef DEBUG
#include "ref_cnt.h"
#endif

namespace {

struct a0_pubsub_metadata_t {
  uint64_t transport_seq;
};

};  // namespace

/////////////////
//  Publisher  //
/////////////////

struct a0_publisher_raw_impl_s {
  a0_transport_t transport;
};

errno_t a0_publisher_raw_init(a0_publisher_raw_t* pub, a0_arena_t arena) {
  auto impl = std::make_unique<a0_publisher_raw_impl_t>();

  a0_transport_init_status_t init_status;
  a0_locked_transport_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&impl->transport, arena, &init_status, &tlk));
  bool empty;
  A0_RETURN_ERR_ON_ERR(a0_transport_empty(tlk, &empty));
  if (empty) {
    A0_RETURN_ERR_ON_ERR(a0_transport_init_metadata(tlk, sizeof(a0_pubsub_metadata_t)));
  }
  a0_transport_unlock(tlk);

#ifdef DEBUG
  a0_ref_cnt_inc(arena.ptr);
#endif

  pub->_impl = impl.release();

  return A0_OK;
}

errno_t a0_publisher_raw_close(a0_publisher_raw_t* pub) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

#ifdef DEBUG
  a0_ref_cnt_dec(pub->_impl->transport._arena.ptr);
#endif

  a0_transport_close(&pub->_impl->transport);
  delete pub->_impl;
  pub->_impl = nullptr;

  return A0_OK;
}

errno_t a0_pub_raw(a0_publisher_raw_t* pub, const a0_packet_t pkt) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

  a0::scoped_transport_lock stlk(&pub->_impl->transport);
  a0_alloc_t alloc;
  A0_RETURN_ERR_ON_ERR(a0_transport_allocator(&stlk.tlk, &alloc));
  A0_RETURN_ERR_ON_ERR(a0_packet_serialize(pkt, alloc, nullptr));
  return a0_transport_commit(stlk.tlk);
}

static constexpr std::string_view TRANSPORT_SEQ = "a0_transport_seq";
static constexpr std::string_view PUBLISHER_SEQ = "a0_publisher_seq";
static constexpr std::string_view PUBLISHER_ID = "a0_publisher_id";

struct a0_publisher_impl_s {
  a0_publisher_raw_t raw;
  uint64_t publisher_seq{0};
  a0_uuid_t id;
};

errno_t a0_publisher_init(a0_publisher_t* pub, a0_arena_t arena) {
  pub->_impl = new a0_publisher_impl_t;
  a0_uuidv4(pub->_impl->id);
  return a0_publisher_raw_init(&pub->_impl->raw, arena);
}

errno_t a0_publisher_close(a0_publisher_t* pub) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

  a0_publisher_raw_close(&pub->_impl->raw);
  delete pub->_impl;
  pub->_impl = nullptr;

  return A0_OK;
}

errno_t a0_pub(a0_publisher_t* pub, const a0_packet_t pkt) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

  uint64_t time_mono;
  a0_time_mono_now(&time_mono);
  char mono_str[20];
  a0_time_mono_str(time_mono, mono_str);

  timespec time_wall;
  a0_time_wall_now(&time_wall);
  char wall_str[36];
  a0_time_wall_str(time_wall, wall_str);

  char pseq_str[20];
  a0::to_chars(pseq_str, pseq_str + 20, pub->_impl->publisher_seq++);

  char tseq_str[20];
  {
    a0::scoped_transport_lock stlk(&pub->_impl->raw._impl->transport);
    a0_buf_t metadata;
    a0_transport_metadata(stlk.tlk, &metadata);
    a0::to_chars(tseq_str, tseq_str + 20, ((a0_pubsub_metadata_t*)metadata.ptr)->transport_seq++);
  }

  constexpr size_t num_extra_headers = 5;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {A0_TIME_MONO, mono_str},
      {A0_TIME_WALL, wall_str},
      {TRANSPORT_SEQ.data(), tseq_str},
      {PUBLISHER_SEQ.data(), pseq_str},
      {PUBLISHER_ID.data(), pub->_impl->id},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_pub_raw(&pub->_impl->raw, full_pkt);
}

//////////////////
//  Subscriber  //
//////////////////

// Synchronous zero-copy version.

struct a0_subscriber_sync_zc_impl_s {
  a0_transport_t transport;

  a0_subscriber_init_t sub_init;
  a0_subscriber_iter_t sub_iter;

  bool read_first{false};
};

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_arena_t arena,
                                   a0_subscriber_init_t sub_init,
                                   a0_subscriber_iter_t sub_iter) {
  sub_sync_zc->_impl = new a0_subscriber_sync_zc_impl_t;
  sub_sync_zc->_impl->sub_init = sub_init;
  sub_sync_zc->_impl->sub_iter = sub_iter;

  a0_transport_init_status_t init_status;
  a0_locked_transport_t tlk;
  a0_transport_init(&sub_sync_zc->_impl->transport,
                    arena,
                    &init_status,
                    &tlk);
  a0_transport_unlock(tlk);

#ifdef DEBUG
  a0_ref_cnt_inc(arena.ptr);
#endif

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t* sub_sync_zc) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

#ifdef DEBUG
  a0_ref_cnt_dec(sub_sync_zc->_impl->transport._arena.ptr);
#endif

  a0_transport_close(&sub_sync_zc->_impl->transport);
  delete sub_sync_zc->_impl;
  sub_sync_zc->_impl = nullptr;

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t* sub_sync_zc, bool* has_next) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

  a0::scoped_transport_lock stlk(&sub_sync_zc->_impl->transport);
  return a0_transport_has_next(stlk.tlk, has_next);
}

errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_zero_copy_callback_t cb) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

  a0::scoped_transport_lock stlk(&sub_sync_zc->_impl->transport);

  if (!sub_sync_zc->_impl->read_first) {
    if (sub_sync_zc->_impl->sub_init == A0_INIT_OLDEST) {
      a0_transport_jump_head(stlk.tlk);
    } else if (sub_sync_zc->_impl->sub_init == A0_INIT_MOST_RECENT ||
               sub_sync_zc->_impl->sub_init == A0_INIT_AWAIT_NEW) {
      a0_transport_jump_tail(stlk.tlk);
    }
  } else {
    if (sub_sync_zc->_impl->sub_iter == A0_ITER_NEXT) {
      a0_transport_next(stlk.tlk);
    } else if (sub_sync_zc->_impl->sub_iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(stlk.tlk);
    }
  }

  a0_transport_frame_t frame;
  a0_transport_frame(stlk.tlk, &frame);

  thread_local a0::scope<a0_alloc_t> headers_alloc = a0::scope_realloc();

  a0_packet_t pkt;
  a0_packet_deserialize(a0::buf(frame), *headers_alloc, &pkt);

  cb.fn(cb.user_data, stlk.tlk, pkt);
  sub_sync_zc->_impl->read_first = true;

  return A0_OK;
}

// Synchronous allocated version.

struct a0_subscriber_sync_impl_s {
  a0_subscriber_sync_zc_t sub_sync_zc;

  a0_alloc_t alloc;
};

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                a0_arena_t arena,
                                a0_alloc_t alloc,
                                a0_subscriber_init_t sub_init,
                                a0_subscriber_iter_t sub_iter) {
  sub_sync->_impl = new a0_subscriber_sync_impl_t;

  sub_sync->_impl->alloc = alloc;
  return a0_subscriber_sync_zc_init(&sub_sync->_impl->sub_sync_zc, arena, sub_init, sub_iter);
}

errno_t a0_subscriber_sync_close(a0_subscriber_sync_t* sub_sync) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  a0_subscriber_sync_zc_close(&sub_sync->_impl->sub_sync_zc);
  delete sub_sync->_impl;
  sub_sync->_impl = nullptr;

  return A0_OK;
}

errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t* sub_sync, bool* has_next) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  return a0_subscriber_sync_zc_has_next(&sub_sync->_impl->sub_sync_zc, has_next);
}

errno_t a0_subscriber_sync_next(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  struct data_t {
    a0_alloc_t alloc;
    a0_packet_t* pkt;
  } data{sub_sync->_impl->alloc, pkt};

  a0_zero_copy_callback_t wrapped_cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_locked_transport_t, a0_packet_t pkt_zc) {
            auto* data = (data_t*)user_data;
            a0_packet_deep_copy(pkt_zc, data->alloc, data->pkt);
          },
  };
  return a0_subscriber_sync_zc_next(&sub_sync->_impl->sub_sync_zc, wrapped_cb);
}

// Zero-copy threaded version.

struct a0_subscriber_zc_impl_s {
  a0::transport_thread worker;
  bool started_empty;
};

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
                              a0_arena_t arena,
                              a0_subscriber_init_t sub_init,
                              a0_subscriber_iter_t sub_iter,
                              a0_zero_copy_callback_t onmsg) {
  sub_zc->_impl = new a0_subscriber_zc_impl_t;

  auto on_transport_init = [sub_zc, sub_init](a0_locked_transport_t tlk,
                                              a0_transport_init_status_t) -> errno_t {
    // TODO(lshamis): Validate transport.

    a0_transport_empty(tlk, &sub_zc->_impl->started_empty);
    if (!sub_zc->_impl->started_empty) {
      if (sub_init == A0_INIT_OLDEST) {
        a0_transport_jump_head(tlk);
      } else if (sub_init == A0_INIT_MOST_RECENT || sub_init == A0_INIT_AWAIT_NEW) {
        a0_transport_jump_tail(tlk);
      }
    }

    return A0_OK;
  };

  auto handle_pkt = [onmsg](a0_locked_transport_t tlk) {
    a0_transport_frame_t frame;
    a0_transport_frame(tlk, &frame);

    thread_local a0::scope<a0_alloc_t> headers_alloc = a0::scope_realloc();

    a0_packet_t pkt;
    a0_packet_deserialize(a0::buf(frame), *headers_alloc, &pkt);

    onmsg.fn(onmsg.user_data, tlk, pkt);
  };

  auto on_transport_nonempty = [sub_zc, sub_init, handle_pkt](a0_locked_transport_t tlk) {
    bool reset = false;
    if (sub_zc->_impl->started_empty) {
      reset = true;
    } else {
      bool ptr_valid;
      a0_transport_ptr_valid(tlk, &ptr_valid);
      reset = !ptr_valid;
    }

    if (reset) {
      a0_transport_jump_head(tlk);
    }

    if (reset || sub_init == A0_INIT_OLDEST || sub_init == A0_INIT_MOST_RECENT) {
      handle_pkt(tlk);
    }
  };

  auto on_transport_hasnext = [sub_iter, handle_pkt](a0_locked_transport_t tlk) {
    if (sub_iter == A0_ITER_NEXT) {
      a0_transport_next(tlk);
    } else if (sub_iter == A0_ITER_NEWEST) {
      a0_transport_jump_tail(tlk);
    }

    handle_pkt(tlk);
  };

  return sub_zc->_impl->worker.init(arena,
                                    on_transport_init,
                                    on_transport_nonempty,
                                    on_transport_hasnext);
}

errno_t a0_subscriber_zc_async_close(a0_subscriber_zc_t* sub_zc, a0_callback_t onclose) {
  if (!sub_zc || !sub_zc->_impl) {
    return ESHUTDOWN;
  }

  sub_zc->_impl->worker.async_close([sub_zc, onclose]() {
    delete sub_zc->_impl;
    sub_zc->_impl = nullptr;

    if (onclose.fn) {
      onclose.fn(onclose.user_data);
    }
  });

  return A0_OK;
}

errno_t a0_subscriber_zc_close(a0_subscriber_zc_t* sub_zc) {
  if (!sub_zc || !sub_zc->_impl) {
    return ESHUTDOWN;
  }

  sub_zc->_impl->worker.await_close();
  delete sub_zc->_impl;
  sub_zc->_impl = nullptr;

  return A0_OK;
}

// Normal threaded version.

struct a0_subscriber_impl_s {
  a0_subscriber_zc_t sub_zc;

  a0_alloc_t alloc;
  a0_packet_callback_t onmsg;
};

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_arena_t arena,
                           a0_alloc_t alloc,
                           a0_subscriber_init_t sub_init,
                           a0_subscriber_iter_t sub_iter,
                           a0_packet_callback_t onmsg) {
  sub->_impl = new a0_subscriber_impl_t;

  sub->_impl->alloc = alloc;
  sub->_impl->onmsg = onmsg;

  a0_zero_copy_callback_t wrapped_onmsg = {
      .user_data = sub->_impl,
      .fn =
          [](void* data, a0_locked_transport_t tlk, a0_packet_t pkt_zc) {
            auto* impl = (a0_subscriber_impl_t*)data;
            a0_packet_t pkt;
            a0_packet_deep_copy(pkt_zc, impl->alloc, &pkt);

            a0::scoped_transport_unlock stulk(tlk);
            impl->onmsg.fn(impl->onmsg.user_data, pkt);
          },
  };

  return a0_subscriber_zc_init(&sub->_impl->sub_zc, arena, sub_init, sub_iter, wrapped_onmsg);
}

errno_t a0_subscriber_close(a0_subscriber_t* sub) {
  if (!sub || !sub->_impl) {
    return ESHUTDOWN;
  }

  auto err = a0_subscriber_zc_close(&sub->_impl->sub_zc);
  delete sub->_impl;
  sub->_impl = nullptr;

  return err;
}

errno_t a0_subscriber_async_close(a0_subscriber_t* sub, a0_callback_t onclose) {
  if (!sub || !sub->_impl) {
    return ESHUTDOWN;
  }

  struct heap_data {
    a0_subscriber_t* sub_;
    a0_callback_t onclose_;
  };

  a0_callback_t cb = {
      .user_data = new heap_data{sub, onclose},
      .fn = [](void* user_data) {
        auto* data = (heap_data*)user_data;
        delete data->sub_->_impl;
        data->sub_->_impl = nullptr;
        if (data->onclose_.fn) {
          data->onclose_.fn(data->onclose_.user_data);
        }
        delete data;
      },
  };

  // clang-tidy thinks the new heap_data is a leak.
  // It can't track it through the callback.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  return a0_subscriber_zc_async_close(&sub->_impl->sub_zc, cb);
}

// One-off reader.

errno_t a0_subscriber_read_one(a0_arena_t arena,
                               a0_alloc_t alloc,
                               a0_subscriber_init_t sub_init,
                               int flags,
                               a0_packet_t* out) {
  if (flags & O_NDELAY || flags & O_NONBLOCK) {
    if (sub_init == A0_INIT_AWAIT_NEW) {
      return EAGAIN;
    }

    a0_subscriber_sync_t sub_sync;
    A0_RETURN_ERR_ON_ERR(a0_subscriber_sync_init(&sub_sync, arena, alloc, sub_init, A0_ITER_NEXT));
    struct sub_guard {
      a0_subscriber_sync_t* sub_sync;
      ~sub_guard() {
        a0_subscriber_sync_close(sub_sync);
      }
    } sub_guard_{&sub_sync};

    bool has_next;
    A0_RETURN_ERR_ON_ERR(a0_subscriber_sync_has_next(&sub_sync, &has_next));
    if (!has_next) {
      return EAGAIN;
    }
    A0_RETURN_ERR_ON_ERR(a0_subscriber_sync_next(&sub_sync, out));
  } else {
    struct data_ {
      a0_packet_t* pkt;

      a0::Event sub_event{};
      a0::Event done_event{};
    } data{.pkt = out};

    a0_packet_callback_t cb = {
        .user_data = &data,
        .fn =
            [](void* user_data, a0_packet_t pkt) {
              auto* data = (data_*)user_data;
              if (data->done_event.is_set()) {
                return;
              }

              data->sub_event.wait();
              *data->pkt = pkt;
              data->done_event.set();
            },
    };

    a0_subscriber_t sub;
    A0_RETURN_ERR_ON_ERR(a0_subscriber_init(&sub, arena, alloc, sub_init, A0_ITER_NEXT, cb));

    data.sub_event.set();
    data.done_event.wait();

    A0_RETURN_ERR_ON_ERR(a0_subscriber_close(&sub));
  }

  return A0_OK;
}
