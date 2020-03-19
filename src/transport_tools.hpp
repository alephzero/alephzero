#pragma once

#include <a0/alloc.h>
#include <a0/transport.h>

#include <chrono>
#include <functional>
#include <thread>

#include "sync.hpp"

#ifdef __cpp_lib_to_chars
#include <charconv>
namespace a0 {
namespace {

using std::to_chars;

}  // namespace
}  // namespace a0
#else
namespace a0 {
namespace {

// TODO: This is about 5x slower then std::to_chars.
inline void to_chars(char* start, char* end, uint64_t val) {
  (void)end;
  auto tmp = std::to_string(val);
  strcpy(start, tmp.c_str());
}

}  // namespace
}  // namespace a0
#endif

static const char kMonoTime[] = "a0_mono_time";
static const char kWallTime[] = "a0_wall_time";

namespace a0 {

inline a0_buf_t buf(a0_transport_frame_t frame) {
  return a0_buf_t{
      .ptr = frame.data,
      .size = frame.hdr.data_size,
  };
}

inline const char* find_header(a0_packet_t pkt, const char* key) {
  for (const a0_packet_headers_block_t* block = &pkt.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* hdr = &block->headers[i];
      if (!strcmp(key, hdr->key)) {
        return hdr->val;
      }
    }
  }
  return nullptr;
}

inline void time_strings(char mono_str[20], char wall_str[36]) {
  // Mono time as unsigned integer with up to 20 chars: "18446744072709551615"
  timespec mono_ts;
  clock_gettime(CLOCK_MONOTONIC, &mono_ts);

  a0::to_chars(mono_str, mono_str + 19, mono_ts.tv_sec * uint64_t(1e9) + mono_ts.tv_nsec);
  mono_str[19] = '\0';

  // Wall time in RFC 3999 Nano: "2006-01-02T15:04:05.999999999Z07:00"
  timespec wall_ts;
  clock_gettime(CLOCK_REALTIME, &wall_ts);

  std::tm wall_tm;
  gmtime_r(&wall_ts.tv_sec, &wall_tm);

  std::strftime(&wall_str[0], 20, "%Y-%m-%dT%H:%M:%S", &wall_tm);
  std::snprintf(&wall_str[19], 17, ".%09ldZ00:00", wall_ts.tv_nsec);
  wall_str[35] = '\0';
}

struct scoped_transport_lock {
  a0_locked_transport_t tlk;

  scoped_transport_lock(a0_transport_t* transport) {
    a0_transport_lock(transport, &tlk);
  }
  ~scoped_transport_lock() {
    a0_transport_unlock(tlk);
  }
};

struct scoped_transport_unlock {
  a0_transport_t* transport;

  scoped_transport_unlock(a0_locked_transport_t tlk) : transport{tlk.transport} {
    a0_transport_unlock(tlk);
  }
  ~scoped_transport_unlock() {
    a0_locked_transport_t tlk;
    a0_transport_lock(transport, &tlk);
  }
};

inline a0_alloc_t transport_allocator(a0_locked_transport_t* tlk) {
  return a0_alloc_t{
      .user_data = tlk,
      .fn =
          [](void* data, size_t size, a0_buf_t* out) {
            a0_transport_frame_t frame;
            a0_transport_alloc(*(a0_locked_transport_t*)data, size, &frame);
            *out = buf(frame);
          },
  };
}

struct transport_thread {
  struct state_t {
    a0_transport_t transport;
    std::thread::id t_id;

    std::function<void(a0_locked_transport_t)> on_transport_nonempty;
    std::function<void(a0_locked_transport_t)> on_transport_hasnext;

    a0::sync<std::function<void()>> onclose;

    bool handle_first_pkt() {
      scoped_transport_lock stlk(&transport);
      if (a0_transport_await(stlk.tlk, a0_transport_nonempty) == A0_OK) {
        on_transport_nonempty(stlk.tlk);
        return true;
      }
      return false;
    }

    bool handle_next_pkt() {
      scoped_transport_lock stlk(&transport);
      if (a0_transport_await(stlk.tlk, a0_transport_has_next) == A0_OK) {
        on_transport_hasnext(stlk.tlk);
        return true;
      }
      return false;
    }

    void thread_main() {
      if (handle_first_pkt()) {
        while (handle_next_pkt()) {
        }
      }

      onclose.with_lock([](auto* fn) {
        if (*fn) {
          (*fn)();
        }
      });
    }
  };

  std::shared_ptr<state_t> state;

  errno_t init(a0_buf_t arena,
               size_t metadata_size,
               std::function<errno_t(a0_locked_transport_t, a0_transport_init_status_t)> on_transport_init,
               std::function<void(a0_locked_transport_t)> on_transport_nonempty,
               std::function<void(a0_locked_transport_t)> on_transport_hasnext) {
    state = std::make_shared<state_t>();
    state->on_transport_nonempty = on_transport_nonempty;
    state->on_transport_hasnext = on_transport_hasnext;

    a0_transport_init_status_t init_status;
    a0_locked_transport_t tlk;
    a0_transport_init(&state->transport, arena, metadata_size, &init_status, &tlk);
    errno_t err = on_transport_init(tlk, init_status);
    a0_transport_unlock(tlk);
    if (err) {
      return err;
    }
    std::thread t([state_ = state]() {
      state_->thread_main();
    });
    state->t_id = t.get_id();
    t.detach();
    return A0_OK;
  }

  errno_t async_close(std::function<void()> onclose) {
    if (!state) {
      return ESHUTDOWN;
    }

    state->onclose.set(onclose);
    a0_transport_close(&state->transport);

    return A0_OK;
  }

  errno_t await_close() {
    if (!state) {
      return ESHUTDOWN;
    }
    if (std::this_thread::get_id() == state->t_id) {
      return EDEADLK;
    }

    a0::Event close_event;
    async_close([&]() {
      close_event.set();
    });
    close_event.wait();

    return A0_OK;
  }
};

}  // namespace a0
