#include <a0/pubsub.h>

#include <string.h>

#include <condition_variable>
#include <mutex>

#include "macros.h"
#include "packet_tools.h"
#include "stream_tools.hh"
#include "strutil.hh"

/////////////////////
//  Pubsub Common  //
/////////////////////

A0_STATIC_INLINE
a0_stream_protocol_t protocol_info() {
  static a0_stream_protocol_t protocol = []() {
    static const char kProtocolName[] = "a0_pubsub";

    a0_stream_protocol_t p;
    p.name.size = strlen(kProtocolName);
    p.name.ptr = (uint8_t*)kProtocolName;

    p.major_version = 0;
    p.minor_version = 1;
    p.patch_version = 0;

    p.metadata_size = 0;

    return p;
  }();

  return protocol;
}

/////////////////
//  Publisher  //
/////////////////

struct a0_publisher_impl_s {
  a0_stream_t stream;
};

errno_t a0_publisher_init(a0_publisher_t* pub, a0_shmobj_t shmobj) {
  pub->_impl = new a0_publisher_impl_t;

  a0_stream_init_status_t init_status;
  a0_locked_stream_t slk;
  a0_stream_init(&pub->_impl->stream, shmobj, protocol_info(), &init_status, &slk);

  if (init_status == A0_STREAM_CREATED) {
    // TODO: Add metadata...
  }

  a0_unlock_stream(slk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
    // TODO: Report error?
  }

  return A0_OK;
}

errno_t a0_publisher_close(a0_publisher_t* pub) {
  if (!pub->_impl) {
    return ESHUTDOWN;
  }

  a0_stream_close(&pub->_impl->stream);
  delete pub->_impl;
  pub->_impl = nullptr;

  return A0_OK;
}

errno_t a0_pub(a0_publisher_t* pub, const a0_packet_t pkt) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

  constexpr size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[num_extra_headers];

  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  std::string clock_str = std::to_string(clock_val);
  extra_headers[0].key = kSendClock;
  extra_headers[0].val = clock_str.c_str();

  // TODO: Add sequence numbers.

  a0::sync_stream_t ss{&pub->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers(num_extra_headers,
                                           extra_headers,
                                           pkt,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    return a0_stream_commit(slk);
  });
}

//////////////////
//  Subscriber  //
//////////////////

// Synchronous zero-copy version.

struct a0_subscriber_sync_zc_impl_s {
  a0_stream_t stream;

  a0_subscriber_init_t sub_init;
  a0_subscriber_iter_t sub_iter;

  bool read_first{false};
};

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_shmobj_t shmobj,
                                   a0_subscriber_init_t sub_init,
                                   a0_subscriber_iter_t sub_iter) {
  sub_sync_zc->_impl = new a0_subscriber_sync_zc_impl_t;
  sub_sync_zc->_impl->sub_init = sub_init;
  sub_sync_zc->_impl->sub_iter = sub_iter;

  a0_stream_init_status_t init_status;
  a0_locked_stream_t slk;
  a0_stream_init(&sub_sync_zc->_impl->stream, shmobj, protocol_info(), &init_status, &slk);

  if (init_status == A0_STREAM_CREATED) {
    // TODO: Add metadata...
  }

  a0_unlock_stream(slk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
    // TODO: Report error?
  }

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t* sub_sync_zc) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

  a0_stream_close(&sub_sync_zc->_impl->stream);
  delete sub_sync_zc->_impl;
  sub_sync_zc->_impl = nullptr;

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t* sub_sync_zc, bool* has_next) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

  a0::sync_stream_t ss{&sub_sync_zc->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    return a0_stream_has_next(slk, has_next);
  });
}

errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_zero_copy_callback_t cb) {
  if (!sub_sync_zc || !sub_sync_zc->_impl) {
    return ESHUTDOWN;
  }

  a0::sync_stream_t ss{&sub_sync_zc->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    if (!sub_sync_zc->_impl->read_first) {
      if (sub_sync_zc->_impl->sub_init == A0_INIT_OLDEST) {
        a0_stream_jump_head(slk);
      } else if (sub_sync_zc->_impl->sub_init == A0_INIT_MOST_RECENT) {
        a0_stream_jump_tail(slk);
      } else if (sub_sync_zc->_impl->sub_init == A0_INIT_AWAIT_NEW) {
        a0_stream_jump_tail(slk);
      }
    } else {
      if (sub_sync_zc->_impl->sub_iter == A0_ITER_NEXT) {
        a0_stream_next(slk);
      } else if (sub_sync_zc->_impl->sub_iter == A0_ITER_NEWEST) {
        a0_stream_jump_tail(slk);
      }
    }

    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);
    cb.fn(cb.user_data, slk, a0::buf(frame));
    sub_sync_zc->_impl->read_first = true;

    return A0_OK;
  });
}

// Synchronous allocated version.

struct a0_subscriber_sync_impl_s {
  a0_subscriber_sync_zc_t sub_sync_zc;

  a0_alloc_t alloc;
};

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                a0_shmobj_t shmobj,
                                a0_alloc_t alloc,
                                a0_subscriber_init_t sub_init,
                                a0_subscriber_iter_t sub_iter) {
  sub_sync->_impl = new a0_subscriber_sync_impl_t;

  sub_sync->_impl->alloc = alloc;
  return a0_subscriber_sync_zc_init(&sub_sync->_impl->sub_sync_zc, shmobj, sub_init, sub_iter);
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
          [](void* user_data, a0_locked_stream_t, a0_packet_t pkt_zc) {
            auto* data = (data_t*)user_data;
            data->alloc.fn(data->alloc.user_data, pkt_zc.size, data->pkt);
            memcpy(data->pkt->ptr, pkt_zc.ptr, data->pkt->size);
          },
  };
  return a0_subscriber_sync_zc_next(&sub_sync->_impl->sub_sync_zc, wrapped_cb);
}

// Zero-copy threaded version.

struct a0_subscriber_zc_impl_s {
  a0::stream_thread worker;
};

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
                              a0_shmobj_t shmobj,
                              a0_subscriber_init_t sub_init,
                              a0_subscriber_iter_t sub_iter,
                              a0_zero_copy_callback_t onmsg) {
  sub_zc->_impl = new a0_subscriber_zc_impl_t;

  auto on_stream_init = [](a0_locked_stream_t, a0_stream_init_status_t) -> errno_t {
    // TODO
    return A0_OK;
  };

  auto read_current_packet = [onmsg](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);
    onmsg.fn(onmsg.user_data, slk, a0::buf(frame));
  };

  auto on_stream_nonempty = [sub_init, read_current_packet](a0_locked_stream_t slk) {
    if (sub_init == A0_INIT_OLDEST) {
      a0_stream_jump_head(slk);
      read_current_packet(slk);
    } else if (sub_init == A0_INIT_MOST_RECENT) {
      a0_stream_jump_tail(slk);
      read_current_packet(slk);
    } else if (sub_init == A0_INIT_AWAIT_NEW) {
      a0_stream_jump_tail(slk);
    }
  };

  auto on_stream_hasnext = [sub_iter, read_current_packet](a0_locked_stream_t slk) {
    if (sub_iter == A0_ITER_NEXT) {
      a0_stream_next(slk);
    } else if (sub_iter == A0_ITER_NEWEST) {
      a0_stream_jump_tail(slk);
    }

    read_current_packet(slk);
  };

  return sub_zc->_impl->worker.init(shmobj,
                                    protocol_info(),
                                    on_stream_init,
                                    on_stream_nonempty,
                                    on_stream_hasnext);
}

errno_t a0_subscriber_zc_async_close(a0_subscriber_zc_t* sub_zc, a0_callback_t onclose) {
  if (!sub_zc || !sub_zc->_impl) {
    return ESHUTDOWN;
  }

  auto worker_ = sub_zc->_impl->worker;
  delete sub_zc->_impl;
  sub_zc->_impl = nullptr;

  worker_.async_close([onclose]() {
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

  auto worker_ = sub_zc->_impl->worker;
  delete sub_zc->_impl;
  sub_zc->_impl = nullptr;
  worker_.await_close();

  return A0_OK;
}

// Normal threaded version.

struct a0_subscriber_impl_s {
  a0_subscriber_zc_t sub_zc;

  a0_alloc_t alloc;
  a0_packet_callback_t onmsg;
};

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_shmobj_t shmobj,
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
          [](void* data, a0_locked_stream_t slk, a0_packet_t pkt_zc) {
            auto* impl = (a0_subscriber_impl_t*)data;
            a0_packet_t pkt;
            impl->alloc.fn(impl->alloc.user_data, pkt_zc.size, &pkt);
            memcpy(pkt.ptr, pkt_zc.ptr, pkt.size);
            a0_unlock_stream(slk);
            impl->onmsg.fn(impl->onmsg.user_data, pkt);
            a0_lock_stream(slk.stream, &slk);
          },
  };

  return a0_subscriber_zc_init(&sub->_impl->sub_zc, shmobj, sub_init, sub_iter, wrapped_onmsg);
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

  auto err = a0_subscriber_zc_async_close(&sub->_impl->sub_zc, onclose);
  delete sub->_impl;
  sub->_impl = nullptr;

  return err;
}
