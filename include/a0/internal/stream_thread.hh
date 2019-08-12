#pragma once

#include <a0/internal/sync_stream.hh>

#include <functional>
#include <mutex>
#include <thread>

namespace a0 {

class stream_thread {
  struct state_t {
    a0_stream_t stream;

    std::function<void(a0_locked_stream_t)> on_stream_nonempty;
    std::function<void(a0_locked_stream_t)> on_stream_hasnext;

    a0_callback_t onclose;
    std::mutex mu;

    bool handle_first_pkt() {
      a0::sync_stream_t ss{&stream};
      return ss.with_lock([&](a0_locked_stream_t slk) {
        if (a0_stream_await(slk, a0_stream_nonempty)) {
          return false;
        }

        on_stream_nonempty(slk);

        return true;
      });
    }

    bool handle_next_pkt() {
      a0::sync_stream_t ss{&stream};
      return ss.with_lock([&](a0_locked_stream_t slk) {
        if (a0_stream_await(slk, a0_stream_has_next)) {
          return false;
        }

        on_stream_hasnext(slk);

        return true;
      });
    }

    void thread_main() {
      if (handle_first_pkt()) {
        while (handle_next_pkt()) {
        }
      }

      {
        std::unique_lock<std::mutex> lk{mu};
        if (onclose.fn) {
          onclose.fn(onclose.user_data);
        }
      }
    }
  };

  std::shared_ptr<state_t> state;

 public:
  errno_t init(a0_shmobj_t shmobj,
               a0_stream_protocol_t stream_protocol,
               std::function<errno_t(a0_locked_stream_t, a0_stream_init_status_t)> on_stream_init,
               std::function<void(a0_locked_stream_t)> on_stream_nonempty,
               std::function<void(a0_locked_stream_t)> on_stream_hasnext) {
    state = std::make_shared<state_t>();
    state->on_stream_nonempty = on_stream_nonempty;
    state->on_stream_hasnext = on_stream_hasnext;

    a0_stream_init_status_t init_status;
    a0_locked_stream_t slk;
    a0_stream_init(&state->stream, shmobj, stream_protocol, &init_status, &slk);
    errno_t err = on_stream_init(slk, init_status);
    a0_unlock_stream(slk);
    if (err) {
      return err;
    }
    std::thread t([state_ = state]() {
      state_->thread_main();
    });
    t.detach();
    return A0_OK;
  }

  errno_t close(a0_callback_t onclose) {
    if (!state) {
      return ESHUTDOWN;
    }

    auto state_ = state;
    state = nullptr;
    {
      std::unique_lock<std::mutex> lk{state_->mu};
      state_->onclose = onclose;
    }
    a0_stream_close(&state_->stream);
    return A0_OK;
  }
};

}  // namespace a0
