#include <a0/pubsub.h>

#include <a0/internal/macros.h>
#include <a0/internal/strutil.hh>

#include <string.h>

#include <mutex>
#include <random>
#include <thread>

/////////////////////
//  Pubsub Common  //
/////////////////////

namespace {

static const char A0_PUBSUB_PROTOCOL_NAME[] = "a0_pubsub";
static const uint32_t major_version = 0;
static const uint32_t minor_version = 1;
static const uint32_t patch_version = 0;

struct sync_stream_t {
  a0_stream_t* stream{nullptr};

  template <typename Fn>
  auto with_lock(Fn&& fn) {
    struct guard {
      a0_locked_stream_t lk;
      guard(a0_stream_t* stream) {
        a0_lock_stream(stream, &lk);
      }
      ~guard() {
        a0_unlock_stream(lk);
      }
    } scope(stream);
    return fn(scope.lk);
  }
};

a0_stream_protocol_t protocol_info() {
  a0_stream_protocol_t protocol;
  protocol.name.size = sizeof(A0_PUBSUB_PROTOCOL_NAME);
  protocol.name.ptr = (uint8_t*)A0_PUBSUB_PROTOCOL_NAME;
  protocol.major_version = major_version;
  protocol.minor_version = minor_version;
  protocol.patch_version = patch_version;
  protocol.metadata_size = 0;
  return protocol;
}

void uuidv4(char out[16]) {
  static std::random_device rd;
  static std::uniform_int_distribution<uint64_t> dist;

  *(uint64_t*)&out[0] = (dist(rd) & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  *(uint64_t*)&out[8] = (dist(rd) & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
}

}  // namespace

/////////////////
//  Publisher  //
/////////////////

struct a0_publisher_impl_s {
  a0_shmobj_t shmobj;
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

  // TODO: Move "a0_uid" at packet build time to make references easier.
  constexpr size_t num_extra_headers = 2;
  std::pair<std::string, std::string> extra_headers[num_extra_headers] = {
      {"a0_uid", "0000000000000000"},
      {"a0_pub_clock", "00000000"},
  };
  uuidv4((char*)extra_headers[0].second.c_str());
  *(uint64_t*)extra_headers[1].second.c_str() =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();

  size_t expanded_pkt_size = pkt.size;
  for (auto&& kv : extra_headers) {
    expanded_pkt_size += sizeof(size_t) + kv.first.size() + sizeof(size_t) + kv.second.size();
  }

  sync_stream_t ss{&pub->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_stream_frame_t frame;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_alloc(slk, expanded_pkt_size, &frame));

    // Offsets into the pkt (r=read ptr) and frame data (w=write ptr).
    size_t roff = 0;
    size_t woff = 0;

    // Number of headers.
    size_t orig_num_headers = *(size_t*)(pkt.ptr + roff);
    size_t total_num_headers = orig_num_headers + num_extra_headers;

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
    for (size_t i = 0; i < num_extra_headers; i++) {
      // Key offset.
      *(size_t*)(frame.data.ptr + idx_woff) = woff;
      idx_woff += sizeof(size_t);

      // Key content.
      memcpy(frame.data.ptr + woff, extra_headers[i].first.c_str(), extra_headers[i].first.size());
      woff += extra_headers[i].first.size();

      // Val offset.
      *(size_t*)(frame.data.ptr + idx_woff) = woff;
      idx_woff += sizeof(size_t);

      // Val content.
      memcpy(frame.data.ptr + woff,
             extra_headers[i].second.c_str(),
             extra_headers[i].second.size());
      woff += extra_headers[i].second.size();
    }

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

  sync_stream_t ss{&sub_sync->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    return a0_stream_has_next(slk, has_next);
  });
}

errno_t a0_subscriber_sync_next_zero_copy(a0_subscriber_sync_t* sub_sync,
                                          a0_zero_copy_callback_t cb) {
  if (!sub_sync || !sub_sync->_impl) {
    return ESHUTDOWN;
  }

  sync_stream_t ss{&sub_sync->_impl->stream};
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

namespace {

struct subscriber_zero_copy_state {
  a0_stream_t stream;
  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;
  a0_stream_frame_t frame;
  a0_zero_copy_callback_t onmsg;

  a0_callback_t onclose;
  std::mutex mu;

  void read_current_packet(a0_locked_stream_t slk) {
    a0_stream_frame(slk, &frame);
    onmsg.fn(onmsg.user_data, slk, frame.data);
  }

  bool handle_first_pkt() {
    sync_stream_t ss{&stream};
    return ss.with_lock([&](a0_locked_stream_t slk) {
      if (a0_stream_await(slk, a0_stream_nonempty)) {
        return false;
      }

      if (read_start == A0_READ_START_EARLIEST) {
        a0_stream_jump_head(slk);
        read_current_packet(slk);
      } else if (read_start == A0_READ_START_LATEST) {
        a0_stream_jump_tail(slk);
        read_current_packet(slk);
      } else if (read_start == A0_READ_START_NEW) {
        a0_stream_jump_tail(slk);
      }

      return true;
    });
  }

  bool handle_next_pkt() {
    sync_stream_t ss{&stream};
    return ss.with_lock([&](a0_locked_stream_t slk) {
      if (a0_stream_await(slk, a0_stream_has_next)) {
        return false;
      }

      if (read_next == A0_READ_NEXT_SEQUENTIAL) {
        a0_stream_next(slk);
      } else if (read_next == A0_READ_NEXT_RECENT) {
        a0_stream_jump_tail(slk);
      }

      read_current_packet(slk);

      return true;
    });
  }

  void thread_main() {
    if (handle_first_pkt()) {
      while (handle_next_pkt())
        ;
    }

    {
      std::unique_lock<std::mutex> lk{mu};
      if (onclose.fn) {
        onclose.fn(onclose.user_data);
      }
    }
  }
};

}  // namespace

struct a0_subscriber_zero_copy_impl_s {
  std::shared_ptr<subscriber_zero_copy_state> state;
};

errno_t a0_subscriber_zero_copy_init(a0_subscriber_zero_copy_t* sub_zc,
                                     a0_shmobj_t shmobj,
                                     a0_subscriber_read_start_t read_start,
                                     a0_subscriber_read_next_t read_next,
                                     a0_zero_copy_callback_t onmsg) {
  sub_zc->_impl = new a0_subscriber_zero_copy_impl_t;
  sub_zc->_impl->state = std::make_shared<subscriber_zero_copy_state>();

  sub_zc->_impl->state->read_start = read_start;
  sub_zc->_impl->state->read_next = read_next;
  sub_zc->_impl->state->onmsg = onmsg;

  a0_stream_init_status_t init_status;
  a0_locked_stream_t slk;
  a0_stream_init(&sub_zc->_impl->state->stream, shmobj, protocol_info(), &init_status, &slk);

  if (init_status == A0_STREAM_CREATED) {
    // TODO: Add metadata...
  }

  a0_unlock_stream(slk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
    // TODO: Report error?
  }

  std::thread t([state = sub_zc->_impl->state]() {
    state->thread_main();
  });
  t.detach();

  return A0_OK;
}

errno_t a0_subscriber_zero_copy_close(a0_subscriber_zero_copy_t* sub_zc, a0_callback_t onclose) {
  if (!sub_zc->_impl || !sub_zc->_impl->state) {
    return ESHUTDOWN;
  }

  auto state = sub_zc->_impl->state;
  delete sub_zc->_impl;
  sub_zc->_impl = nullptr;
  {
    std::unique_lock<std::mutex> lk{state->mu};
    state->onclose = onclose;
  }
  a0_stream_close(&state->stream);
  return A0_OK;
}

// Multi-threaded version.

struct a0_subscriber_impl_s {
  a0_subscriber_zero_copy_t sub_zc;

  a0_alloc_t alloc;
  a0_subscriber_callback_t user_onmsg;
  a0_callback_t user_onclose;
};

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_shmobj_t shmobj,
                           a0_subscriber_read_start_t read_start,
                           a0_subscriber_read_next_t read_next,
                           a0_alloc_t alloc,
                           a0_subscriber_callback_t onmsg) {
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
