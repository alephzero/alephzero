#pragma once

#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/packet.h>
#include <a0/transport.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "macros.h"
#include "sync.hpp"

#ifdef DEBUG
#include "ref_cnt.h"
#endif

namespace a0 {

A0_STATIC_INLINE
a0_buf_t buf(a0_transport_frame_t frame) {
  return a0_buf_t{
      .ptr = frame.data,
      .size = frame.hdr.data_size,
  };
}

A0_STATIC_INLINE
std::string_view find_header(a0_packet_t pkt, std::string_view key) {
  for (const a0_packet_headers_block_t* block = &pkt.headers_block;
       block;
       block = block->next_block) {
    for (size_t i = 0; i < block->size; i++) {
      a0_packet_header_t* hdr = &block->headers[i];
      if (key == hdr->key) {
        return hdr->val;
      }
    }
  }
  return {};
}

struct scoped_transport_lock {
  a0_locked_transport_t tlk;

  explicit scoped_transport_lock(a0_transport_t* transport) {
    a0_transport_lock(transport, &tlk);
  }
  ~scoped_transport_lock() {
    a0_transport_unlock(tlk);
  }
};

struct scoped_transport_unlock {
  a0_transport_t* transport;

  explicit scoped_transport_unlock(a0_locked_transport_t tlk)
      : transport{tlk.transport} {
    a0_transport_unlock(tlk);
  }
  ~scoped_transport_unlock() {
    a0_locked_transport_t tlk;
    a0_transport_lock(transport, &tlk);
  }
};

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

  errno_t init(
      a0_arena_t arena,
      const std::function<errno_t(a0_locked_transport_t, a0_transport_init_status_t)>& on_transport_init,
      std::function<void(a0_locked_transport_t)> on_transport_nonempty,
      std::function<void(a0_locked_transport_t)> on_transport_hasnext) {
    state = std::make_shared<state_t>();
    state->on_transport_nonempty = std::move(on_transport_nonempty);
    state->on_transport_hasnext = std::move(on_transport_hasnext);

    a0_transport_init_status_t init_status;
    a0_locked_transport_t tlk;
    a0_transport_init(&state->transport, arena, &init_status, &tlk);
    errno_t err = on_transport_init(tlk, init_status);
    a0_transport_unlock(tlk);
    if (err) {
      return err;
    }

#ifdef DEBUG
    a0_ref_cnt_inc(arena.ptr);
#endif

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

#ifdef DEBUG
    a0_ref_cnt_dec(state->transport._arena.ptr);
#endif

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
