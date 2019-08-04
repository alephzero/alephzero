#include <a0/pubsub.h>

#include <a0/internal/macros.h>
#include <a0/internal/strutil.hh>
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

struct a0_subscriber_impl_s {
  int efd;
  a0_shmobj_t shmobj;
  a0_stream_t stream;
  sync_stream_t sync_stream;
  a0_alloc_t alloc;
  a0_subscriber_read_start_t read_start;
  a0_subscriber_read_next_t read_next;
  a0_stream_frame_t frame;
  a0_packet_t pkt;
  std::atomic<bool> data_ready{false};
  std::atomic<bool> closing{false};
  std::thread t;

  std::mutex mu;
  std::condition_variable cv;

  void read_current_packet(a0_locked_stream_t slk) {
    a0_stream_frame(slk, &frame);
    alloc.fn(alloc.user_data, frame.data.size, &pkt);
    memcpy(pkt.ptr, frame.data.ptr, pkt.size);
    data_ready = true;
  }

  void await_user_read() {
    static const uint64_t efd_inc = 1;
    write(efd, &efd_inc, sizeof(uint64_t));
    std::unique_lock<std::mutex> lk{mu};
    cv.wait(lk, [&]() { return !data_ready && !closing; });
  }

  bool handle_first_pkt() {
    bool success = sync_stream.with_lock([&](a0_locked_stream_t slk) {
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

    if (success && data_ready) {
      await_user_read();
    }

    return success;
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
    if (!handle_first_pkt()) {
      return;
    }

    while (!closing && handle_next_pkt()) {
      await_user_read();
    }
  }
};

errno_t a0_subscriber_open(
    a0_subscriber_t* subscriber,
    a0_topic_t topic,
    a0_alloc_t alloc,
    a0_subscriber_read_start_t read_start,
    a0_subscriber_read_next_t read_next,
    int* out_fd) {
  subscriber->_impl = new a0_subscriber_impl_t;
  subscriber->_impl->efd = eventfd(0, 0);
  subscriber->_impl->alloc = alloc;
  subscriber->_impl->read_start = read_start;
  subscriber->_impl->read_next = read_next;

  auto topic_path = unmapped_topic_path(topic);
  a0_shmobj_open(topic_path.c_str(),
                 default_shmobj_options(),
                 &subscriber->_impl->shmobj);

  a0_stream_init_status_t init_status;
  a0_locked_stream_t lk;
  a0_stream_init(
      &subscriber->_impl->stream,
      subscriber->_impl->shmobj,
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

  subscriber->_impl->sync_stream.stream = &subscriber->_impl->stream;
  subscriber->_impl->t = std::thread([subscriber]() {
    subscriber->_impl->thread_main();
  });

  *out_fd = subscriber->_impl->efd;
  return A0_OK;
}

errno_t a0_subscriber_read(a0_subscriber_t* subscriber, a0_packet_t* pkt) {
  if (!subscriber->_impl->data_ready) {
    return EWOULDBLOCK;
  }

  *pkt = subscriber->_impl->pkt;
  subscriber->_impl->data_ready = false;
  subscriber->_impl->cv.notify_one();
  return A0_OK;
}

errno_t a0_subscriber_close(a0_subscriber_t* subscriber) {
  subscriber->_impl->closing = true;
  a0_stream_close(&subscriber->_impl->stream);
  subscriber->_impl->cv.notify_one();
  subscriber->_impl->t.join();
  delete subscriber->_impl;
  return A0_OK;
}
