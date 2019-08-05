#include <a0/pubsub.h>

#include <a0/internal/macros.h>
#include <a0/internal/strutil.hh>
#include <a0/internal/sync.hh>
#include <atomic>
#include <string.h>
#include <string>
#include <thread>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <mutex>
#include <condition_variable>

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

a0_shmobj_options_t* default_shmobj_options() {
  static a0_shmobj_options_t opts = []() {
    a0_shmobj_options_t opts_;
    opts_.size = 32 * 1024 * 1024;  // 32MB
    return opts_;
  }();
  return &opts;
}

void validate_topic(a0_topic_t topic) {
  // TODO: assert topic->name is entirely isalnum + '_' + '.' and does not contain '__'.
  // TODO: assert topic->container is entirely isalnum + '_' + '.' and does not contain '__'.
}

std::string unmapped_topic_path(a0_topic_t topic) {
  validate_topic(topic);
  return a0::strutil::fmt("/%.*s__%.*s__%.*s",
                          sizeof(A0_PUBSUB_PROTOCOL_NAME), A0_PUBSUB_PROTOCOL_NAME,
                          topic.container.size, topic.container.ptr,
                          topic.name.size, topic.name.ptr);
}

}  // namespace

/////////////////
//  Publisher  //
/////////////////

namespace {

std::string publisher_topic_path(a0_topic_t topic) {
  validate_topic(topic);

  if (topic.container.size) {
    return unmapped_topic_path(topic);
  }

  const char* container_name = getenv("A0_CONTAINER_NAME");
  return a0::strutil::fmt("/%.*s__%s__%.*s",
                          sizeof(A0_PUBSUB_PROTOCOL_NAME), A0_PUBSUB_PROTOCOL_NAME,
                          strlen(container_name), container_name,
                          topic.name.size, topic.name.ptr);
}

}  // namespace

struct a0_publisher_impl_s {
  a0_shmobj_t shmobj;
  a0_stream_t stream;
};

errno_t a0_publisher_init(a0_publisher_t* publisher, a0_topic_t topic) {
  publisher->_impl = new a0_publisher_impl_t;

  auto topic_path = publisher_topic_path(topic);
  a0_shmobj_open(topic_path.c_str(),
                 default_shmobj_options(),
                 &publisher->_impl->shmobj);

  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  a0_stream_init(
      &publisher->_impl->stream,
      publisher->_impl->shmobj,
      protocol_info(),
      &init_status,
      &lk);

  if (init_status == A0_STREAM_CREATED) {
    // TODO: Add metadata...
  }

  a0_unlock_stream(lk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
    // TODO: Report error?
  }

  return A0_OK;
}

errno_t a0_publisher_close(a0_publisher_t* publisher) {
  a0_stream_close(&publisher->_impl->stream);
  a0_shmobj_close(&publisher->_impl->shmobj);
  delete publisher->_impl;
  return A0_OK;
}

errno_t a0_pub(a0_publisher_t* publisher, a0_packet_t pkt) {
  sync_stream_t ss{&publisher->_impl->stream};
  return ss.with_lock([&](a0_locked_stream_t lk) {
    a0_stream_frame_t frame;
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_alloc(lk, pkt.size, &frame));
    memcpy(frame.data.ptr, pkt.ptr, pkt.size);
    A0_INTERNAL_RETURN_ERR_ON_ERR(a0_stream_commit(lk));
    return A0_OK;
  });
}

//////////////////
//  Subscriber  //
//////////////////

// Zero-copy multi-threaded version.

namespace {

struct subscriber_zero_copy_state {
  a0_shmobj_t shmobj;
  a0_stream_t stream;
  sync_stream_t sync_stream;
  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;
  a0_stream_frame_t frame;
  a0_subscriber_zero_copy_callback_t onmsg;

  a0::sync<a0_callback_t> onclose;

  void read_current_packet(a0_locked_stream_t slk) {
    a0_stream_frame(slk, &frame);
    onmsg.fn(onmsg.user_data, slk, frame.data);
  }

  bool handle_first_pkt() {
    return sync_stream.with_lock([&](a0_locked_stream_t slk) {
      if (!a0_stream_await(slk, a0_stream_nonempty)) {
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
    return sync_stream.with_lock([&](a0_locked_stream_t slk) {
      if (!a0_stream_await(slk, a0_stream_has_next)) {
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
      while (handle_next_pkt());
    }

    onclose.with_unique_lock([](a0_callback_t& cb) {
      if (cb.fn) {
        cb.fn(cb.user_data);
      }
    });
  }
};

}  // namespace

struct a0_subscriber_zero_copy_impl_s {
  std::shared_ptr<subscriber_zero_copy_state> state;
};

errno_t a0_subscriber_zero_copy_open(
    a0_subscriber_zero_copy_t* sub_zc,
    a0_topic_t topic,
    a0_subscriber_read_start_t read_start,
    a0_subscriber_read_next_t read_next,
    a0_subscriber_zero_copy_callback_t onmsg) {
  sub_zc->_impl = new a0_subscriber_zero_copy_impl_t;
  sub_zc->_impl->state = std::make_shared<subscriber_zero_copy_state>();

  sub_zc->_impl->state->read_start = read_start;
  sub_zc->_impl->state->read_next = read_next;
  sub_zc->_impl->state->onmsg = onmsg;

  auto topic_path = unmapped_topic_path(topic);
  a0_shmobj_open(topic_path.c_str(),
                 default_shmobj_options(),
                 &sub_zc->_impl->state->shmobj);

  a0_stream_init_status_t init_status;
  a0_locked_stream_t slk;
  a0_stream_init(
      &sub_zc->_impl->state->stream,
      sub_zc->_impl->state->shmobj,
      protocol_info(),
      &init_status,
      &slk);

  if (init_status == A0_STREAM_CREATED) {
      // TODO: Add metadata...
  }

  a0_unlock_stream(slk);

  if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
      // TODO: Report error?
  }

  sub_zc->_impl->state->sync_stream.stream = &sub_zc->_impl->state->stream;

  std::thread t([state = sub_zc->_impl->state]() {
    state->thread_main();
  });
  t.detach();

  return A0_OK;
}

errno_t a0_subscriber_zero_copy_close(
    a0_subscriber_zero_copy_t* sub_zc,
    a0_callback_t onclose) {
  if (!sub_zc->_impl || !sub_zc->_impl->state) {
    return EPIPE;
  }

  sub_zc->_impl->state->onclose.with_unique_lock([&](a0_callback_t& cb) {
    cb = onclose;
  });
  a0_stream_close(&sub_zc->_impl->state->stream);
  sub_zc->_impl->state = nullptr;
  delete sub_zc->_impl;
  return A0_OK;
}

// Multi-threaded version.

struct a0_subscriber_impl_s {
  a0_subscriber_zero_copy_t sub_zc;

  a0_alloc_t alloc;
  a0_subscriber_callback_t user_onmsg;
  a0_callback_t user_onclose;
};

errno_t a0_subscriber_open(
    a0_subscriber_t* sub,
    a0_topic_t topic,
    a0_subscriber_read_start_t read_start,
    a0_subscriber_read_next_t read_next,
    a0_alloc_t alloc,
    a0_subscriber_callback_t onmsg) {
  sub->_impl = new a0_subscriber_impl_t;

  sub->_impl->alloc = alloc;
  sub->_impl->user_onmsg = onmsg;

  a0_subscriber_zero_copy_callback_t wrapped_onmsg;
  wrapped_onmsg.user_data = sub->_impl;
  wrapped_onmsg.fn = [](void* data, a0_locked_stream_t slk, a0_packet_t pkt_zc) {
    auto* impl = (a0_subscriber_impl_t*)data;
    a0_packet_t pkt;
    impl->alloc.fn(impl->alloc.user_data, pkt_zc.size, &pkt);
    a0_unlock_stream(slk);
    impl->user_onmsg.fn(impl->user_onmsg.user_data, pkt);
    a0_lock_stream(slk.stream, &slk);
  };

  a0_subscriber_zero_copy_open(
    &sub->_impl->sub_zc,
    topic,
    read_start,
    read_next,
    wrapped_onmsg);

  return A0_OK;
}

errno_t a0_subscriber_close(
    a0_subscriber_t* sub,
    a0_callback_t onclose) {
  sub->_impl->user_onclose = onclose;
  a0_callback_t wrapped_onclose;
  wrapped_onclose.user_data = sub->_impl;
  wrapped_onclose.fn = [](void* data) {
    auto* impl = (a0_subscriber_impl_t*)data;
    impl->user_onclose.fn(impl->user_onclose.user_data);
    delete impl;
  };
  a0_subscriber_zero_copy_close(&sub->_impl->sub_zc, wrapped_onclose);
  return A0_OK;
}

// FD version.

struct a0_subscriber_fd_impl_s {
  a0_subscriber_t sub;
  int efd;

  a0_packet_t cur_pkt;
  std::atomic<bool> data_ready{false};
  std::atomic<bool> closing{false};
  std::mutex mu;
  std::condition_variable cv;
};

errno_t a0_subscriber_fd_open(
    a0_subscriber_fd_t* sub_fd,
    a0_topic_t topic,
    a0_alloc_t alloc,
    a0_subscriber_read_start_t read_start,
    a0_subscriber_read_next_t read_next,
    int* fd_out) {
  sub_fd->_impl = new a0_subscriber_fd_impl_t;
  sub_fd->_impl->efd = eventfd(0, 0);

  a0_subscriber_callback_t onmsg;
  onmsg.user_data = sub_fd->_impl;
  onmsg.fn = [](void* data, a0_packet_t pkt) {
    auto* impl = (a0_subscriber_fd_impl_t*)data;

    if (impl->closing) {
      return;
    }

    std::unique_lock<std::mutex> lk{impl->mu};
    impl->cur_pkt = pkt;
    impl->data_ready = true;
    
    static const uint64_t efd_inc = 1;
    write(impl->efd, &efd_inc, sizeof(uint64_t));

    impl->cv.wait(lk, [impl]() { return !impl->data_ready || impl->closing; });
  };

  a0_subscriber_open(
      &sub_fd->_impl->sub,
      topic,
      read_start,
      read_next,
      alloc,
      onmsg);

  *fd_out = sub_fd->_impl->efd;

  return A0_OK;
}

errno_t a0_subscriber_fd_read(a0_subscriber_fd_t* sub_fd, a0_packet_t* pkt) {
  if (!sub_fd->_impl->data_ready) {
    return EWOULDBLOCK;
  }

  *pkt = sub_fd->_impl->cur_pkt;
  sub_fd->_impl->data_ready = false;
  sub_fd->_impl->cv.notify_one();
  return A0_OK;
}

errno_t a0_subscriber_fd_close(a0_subscriber_fd_t* sub_fd) {
  sub_fd->_impl->closing = true;
  sub_fd->_impl->cv.notify_one();

  a0_callback_t onclose;
  onclose.user_data = sub_fd->_impl;
  onclose.fn = [](void* data) {
    auto* impl = (a0_subscriber_fd_impl_t*)data;;
    close(impl->efd);
    delete impl;
  };
  a0_subscriber_close(&sub_fd->_impl->sub, onclose);
  return A0_OK;
}
