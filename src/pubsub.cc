#include <a0/pubsub.h>

#include <a0/internal/macros.h>
#include <a0/internal/stream_thread.hh>
#include <a0/internal/strutil.hh>
#include <a0/internal/sync_stream.hh>

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
    static const char PROTOCOL_NAME[] = "a0_pubsub";

    a0_stream_protocol_t p;
    p.name.size = strlen(PROTOCOL_NAME);
    p.name.ptr = (uint8_t*)PROTOCOL_NAME;

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

  constexpr size_t extra_headers = 2;
  static const char clock_key[] = "a0_pub_clock";
  uint64_t clock_val = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
  static const char seq_key[] = "a0_stream_seq";
  uint64_t seq_val = 0;  // Get from frame below.

  size_t expanded_pkt_size = pkt.size;
  expanded_pkt_size += sizeof(size_t) + strlen(clock_key) + sizeof(size_t) + sizeof(clock_val);
  expanded_pkt_size += sizeof(size_t) + strlen(seq_key) + sizeof(size_t) + sizeof(seq_val);

  a0::sync_stream_t ss{&pub->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_alloc(slk, expanded_pkt_size, &frame));

    seq_val = frame.hdr.seq;

    // Offsets into the pkt (r=read ptr) and frame data (w=write ptr).
    size_t roff = 0;
    size_t woff = 0;

    // Number of headers.
    size_t orig_num_headers = *(size_t*)(pkt.ptr + roff);
    size_t total_num_headers = orig_num_headers + extra_headers;

    // Write in the new header count.
    *(size_t*)(frame.data.ptr + woff) = total_num_headers;

    roff += sizeof(size_t);
    woff += sizeof(size_t);

    // Offset for the index table.
    size_t idx_roff = roff;
    size_t idx_woff = woff;

    // Update offsets past the end of the index table.
    roff += 2 * orig_num_headers * sizeof(size_t) + sizeof(size_t);
    woff += 2 * total_num_headers * sizeof(size_t) + sizeof(size_t);

    // Add extra headers.

    // Clock key.
    *(size_t*)(frame.data.ptr + idx_woff) = woff;
    idx_woff += sizeof(size_t);

    memcpy(frame.data.ptr + woff, clock_key, strlen(clock_key));
    woff += strlen(clock_key);

    // Clock val.
    *(size_t*)(frame.data.ptr + idx_woff) = woff;
    idx_woff += sizeof(size_t);

    *(uint64_t*)(frame.data.ptr + woff) = clock_val;
    woff += sizeof(uint64_t);

    // Sequence key.
    *(size_t*)(frame.data.ptr + idx_woff) = woff;
    idx_woff += sizeof(size_t);

    memcpy(frame.data.ptr + woff, seq_key, strlen(seq_key));
    woff += strlen(seq_key);

    // Sequence val.
    *(size_t*)(frame.data.ptr + idx_woff) = woff;
    idx_woff += sizeof(size_t);

    *(uint64_t*)(frame.data.ptr + woff) = seq_val;
    woff += sizeof(uint64_t);

    // Add offsets for existing headers.
    for (size_t i = 0; i < 2 * orig_num_headers; i++) {
      *(size_t*)(frame.data.ptr + idx_woff) =
          woff + *(size_t*)(pkt.ptr + idx_roff) - *(size_t*)(pkt.ptr + sizeof(size_t));
      idx_woff += sizeof(size_t);
      idx_roff += sizeof(size_t);
    }

    // Add offset for payload.
    *(size_t*)(frame.data.ptr + idx_woff) =
        woff + *(size_t*)(pkt.ptr + idx_roff) - *(size_t*)(pkt.ptr + sizeof(size_t));

    // Copy existing headers + payload.
    memcpy(frame.data.ptr + woff, pkt.ptr + roff, pkt.size - roff);

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
    cb.fn(cb.user_data, slk, frame.data);
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
    onmsg.fn(onmsg.user_data, slk, frame.data);
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

  sub_zc->_impl->st.close(onclose);
  delete sub_zc->_impl;
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
