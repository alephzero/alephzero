#include <a0/pubsub.h>

#include <a0/internal/macros.h>
#include <a0/internal/packet_tools.h>
#include <a0/internal/stream_tools.hh>
#include <a0/internal/strutil.hh>

#include <string.h>

#include <mutex>
#include <random>
#include <thread>

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
  a0_stream_close(&pub->_impl->stream);
  delete pub->_impl;
  pub->_impl = nullptr;
  return A0_OK;
}

errno_t a0_pub(a0_publisher_t* pub, a0_packet_t pkt) {
  if (!pub || !pub->_impl) {
    return ESHUTDOWN;
  }

  constexpr size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[num_extra_headers];

  static const char clock_key[] = "a0_pub_clock";
  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  extra_headers[0].key = a0_buf_t{
      .ptr = (uint8_t*)clock_key,
      .size = strlen(clock_key),
  };
  extra_headers[0].val = a0_buf_t{
      .ptr = (uint8_t*)&clock_val,
      .size = sizeof(uint64_t),
  };

  // TODO: Add sequence numbers.

  a0::sync_stream_t ss{&pub->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers(num_extra_headers,
                                           extra_headers,
                                           pkt,
                                           a0::stream_allocator(slk),
                                           nullptr);
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_commit(slk));
    return A0_OK;
  });
}

//////////////////
//  Subscriber  //
//////////////////

struct a0_subscriber_sync_impl_s {
  a0_stream_t stream;

  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;

  bool read_first{false};
};

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                a0_shmobj_t shmobj,
                                a0_subscriber_read_start_t read_start,
                                a0_subscriber_read_next_t read_next) {
  sub_sync->_impl = new a0_subscriber_sync_impl_t;
  sub_sync->_impl->read_start = read_start;
  sub_sync->_impl->read_next = read_next;

  a0_stream_init_status_t init_status;
  a0_locked_stream_t slk;
  a0_stream_init(&sub_sync->_impl->stream, shmobj, protocol_info(), &init_status, &slk);

  if (init_status == A0_STREAM_CREATED) {
    // TODO: Add metadata...
  }

  a0_unlock_stream(slk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
    // TODO: Report error?
  }

  return A0_OK;
}

errno_t a0_subscriber_sync_close(a0_subscriber_sync_t* sub_sync) {
  a0_stream_close(&sub_sync->_impl->stream);
  delete sub_sync->_impl;
  sub_sync->_impl = nullptr;
  return A0_OK;
}

errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t* sub_sync, bool* has_next) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  a0::sync_stream_t ss{&sub_sync->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    return a0_stream_has_next(slk, has_next);
  });
}

errno_t a0_subscriber_sync_next_zero_copy(a0_subscriber_sync_t* sub_sync,
                                          a0_zero_copy_callback_t cb) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  a0::sync_stream_t ss{&sub_sync->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    if (!sub_sync->_impl->read_first) {
      if (sub_sync->_impl->read_start == A0_READ_START_EARLIEST) {
        a0_stream_jump_head(slk);
      } else if (sub_sync->_impl->read_start == A0_READ_START_LATEST) {
        a0_stream_jump_tail(slk);
      } else if (sub_sync->_impl->read_start == A0_READ_START_NEW) {
        a0_stream_jump_tail(slk);
      }
    } else {
      if (sub_sync->_impl->read_next == A0_READ_NEXT_SEQUENTIAL) {
        a0_stream_next(slk);
      } else if (sub_sync->_impl->read_next == A0_READ_NEXT_RECENT) {
        a0_stream_jump_tail(slk);
      }
    }

    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);
    cb.fn(cb.user_data, slk, a0::buf(frame));
    sub_sync->_impl->read_first = true;

    return A0_OK;
  });
}

errno_t a0_subscriber_sync_next(a0_subscriber_sync_t* sub_sync,
                                a0_alloc_t alloc,
                                a0_packet_t* pkt) {
  struct data_t {
    a0_alloc_t alloc;
    a0_packet_t* pkt;
  } data{alloc, pkt};

  a0_zero_copy_callback_t wrapped_cb = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_locked_stream_t, a0_packet_t pkt_zc) {
            auto* data = (data_t*)user_data;
            data->alloc.fn(data->alloc.user_data, pkt_zc.size, data->pkt);
            memcpy(data->pkt->ptr, pkt_zc.ptr, data->pkt->size);
          },
  };
  return a0_subscriber_sync_next_zero_copy(sub_sync, wrapped_cb);
}

// Zero-copy multi-threaded version.

struct a0_subscriber_zero_copy_impl_s {
  a0::stream_thread st;
};

errno_t a0_subscriber_zero_copy_init(a0_subscriber_zero_copy_t* sub_zc,
                                     a0_shmobj_t shmobj,
                                     a0_subscriber_read_start_t read_start,
                                     a0_subscriber_read_next_t read_next,
                                     a0_zero_copy_callback_t onmsg) {
  sub_zc->_impl = new a0_subscriber_zero_copy_impl_t;

  auto on_stream_init = [](a0_locked_stream_t, a0_stream_init_status_t) -> errno_t {
    // TODO
    return A0_OK;
  };

  auto read_current_packet = [onmsg](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    a0_stream_frame(slk, &frame);
    onmsg.fn(onmsg.user_data, slk, a0::buf(frame));
  };

  auto on_stream_nonempty = [read_start, read_current_packet](a0_locked_stream_t slk) {
    if (read_start == A0_READ_START_EARLIEST) {
      a0_stream_jump_head(slk);
      read_current_packet(slk);
    } else if (read_start == A0_READ_START_LATEST) {
      a0_stream_jump_tail(slk);
      read_current_packet(slk);
    } else if (read_start == A0_READ_START_NEW) {
      a0_stream_jump_tail(slk);
    }
  };

  auto on_stream_hasnext = [read_next, read_current_packet](a0_locked_stream_t slk) {
    if (read_next == A0_READ_NEXT_SEQUENTIAL) {
      a0_stream_next(slk);
    } else if (read_next == A0_READ_NEXT_RECENT) {
      a0_stream_jump_tail(slk);
    }

    read_current_packet(slk);
  };

  return sub_zc->_impl->st.init(shmobj,
                                protocol_info(),
                                on_stream_init,
                                on_stream_nonempty,
                                on_stream_hasnext);
}

errno_t a0_subscriber_zero_copy_close(a0_subscriber_zero_copy_t* sub_zc, a0_callback_t onclose) {
  if (!sub_zc->_impl) {
    return ESHUTDOWN;
  }

  auto st_ = sub_zc->_impl->st;
  delete sub_zc->_impl;
  sub_zc->_impl = nullptr;

  st_.close(onclose);
  return A0_OK;
}

// Multi-threaded version.

struct a0_subscriber_impl_s {
  a0_subscriber_zero_copy_t sub_zc;

  a0_alloc_t alloc;
  a0_packet_callback_t user_onmsg;
  a0_callback_t user_onclose;
};

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_shmobj_t shmobj,
                           a0_subscriber_read_start_t read_start,
                           a0_subscriber_read_next_t read_next,
                           a0_alloc_t alloc,
                           a0_packet_callback_t onmsg) {
  sub->_impl = new a0_subscriber_impl_t;

  sub->_impl->alloc = alloc;
  sub->_impl->user_onmsg = onmsg;

  a0_zero_copy_callback_t wrapped_onmsg = {
      .user_data = sub->_impl,
      .fn =
          [](void* data, a0_locked_stream_t slk, a0_packet_t pkt_zc) {
            auto* impl = (a0_subscriber_impl_t*)data;
            a0_packet_t pkt;
            impl->alloc.fn(impl->alloc.user_data, pkt_zc.size, &pkt);
            memcpy(pkt.ptr, pkt_zc.ptr, pkt.size);
            a0_unlock_stream(slk);
            impl->user_onmsg.fn(impl->user_onmsg.user_data, pkt);
            a0_lock_stream(slk.stream, &slk);
          },
  };

  a0_subscriber_zero_copy_init(&sub->_impl->sub_zc, shmobj, read_start, read_next, wrapped_onmsg);

  return A0_OK;
}

errno_t a0_subscriber_close(a0_subscriber_t* sub, a0_callback_t onclose) {
  if (!sub->_impl) {
    return ESHUTDOWN;
  }

  sub->_impl->user_onclose = onclose;
  a0_callback_t wrapped_onclose = {
      .user_data = sub,
      .fn =
          [](void* data) {
            auto* sub = (a0_subscriber_t*)data;
            sub->_impl->user_onclose.fn(sub->_impl->user_onclose.user_data);
            delete sub->_impl;
            sub->_impl = nullptr;
          },
  };
  a0_subscriber_zero_copy_close(&sub->_impl->sub_zc, wrapped_onclose);
  return A0_OK;
}
