#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/heartbeat.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/time.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ratio>
#include <thread>

#include "macros.h"
#include "scope.hpp"
#include "sync.hpp"

const a0_heartbeat_options_t A0_HEARTBEAT_OPTIONS_DEFAULT = {
    .freq = 10,
};

struct a0_heartbeat_impl_s {
  a0_heartbeat_options_t opts;
  a0_publisher_t publisher;
  a0::Event stop_event;
  std::thread thrd;
};

errno_t a0_heartbeat_init(a0_heartbeat_t* h, a0_arena_t arena, const a0_heartbeat_options_t* opts_) {
  const a0_heartbeat_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_HEARTBEAT_OPTIONS_DEFAULT;
  }

  auto impl = std::make_unique<a0_heartbeat_impl_t>();

  impl->opts = *opts;
  A0_RETURN_ERR_ON_ERR(a0_publisher_init(&impl->publisher, arena));

  h->_impl = impl.release();
  h->_impl->thrd = std::thread([impl_ = h->_impl]() {
    while (!impl_->stop_event.is_set()) {
      a0_packet_t pkt;
      a0_packet_init(&pkt);
      pkt.payload.ptr = (uint8_t*)"";
      a0_pub(&impl_->publisher, pkt);
      impl_->stop_event.wait_for(std::chrono::nanoseconds(uint64_t(1e9 / impl_->opts.freq)));
    }
  });

  return A0_OK;
}

errno_t a0_heartbeat_close(a0_heartbeat_t* h) {
  if (!h->_impl) {
    return ESHUTDOWN;
  }

  h->_impl->stop_event.set();
  h->_impl->thrd.join();

  a0_publisher_close(&h->_impl->publisher);

  delete h->_impl;
  h->_impl = nullptr;

  return A0_OK;
}

const a0_heartbeat_listener_options_t A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT = {
    .min_freq = 5,
};

struct a0_heartbeat_listener_impl_s {
  a0_heartbeat_listener_options_t opts;
  a0_subscriber_sync_t sub;
  a0::Event init_event;
  a0::Event stop_event;
  std::thread thrd;

  bool detected;
  a0_callback_t ondetected;
  a0_callback_t onmissed;
};

errno_t a0_heartbeat_listener_init(a0_heartbeat_listener_t* hl,
                                   a0_arena_t arena,
                                   a0_alloc_t alloc,
                                   const a0_heartbeat_listener_options_t* opts_,
                                   a0_callback_t ondetected,
                                   a0_callback_t onmissed) {
  const a0_heartbeat_listener_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT;
  }

  auto impl = std::make_unique<a0_heartbeat_listener_impl_t>();

  impl->opts = *opts;
  impl->detected = false;
  impl->ondetected = ondetected;
  impl->onmissed = onmissed;

  A0_RETURN_ERR_ON_ERR(
      a0_subscriber_sync_init(&impl->sub, arena, alloc, A0_INIT_MOST_RECENT, A0_ITER_NEWEST));

  auto sleep_dur = std::chrono::nanoseconds(uint64_t(1e9 / opts->min_freq));

  hl->_impl = impl.release();
  hl->_impl->thrd = std::thread([impl_ = hl->_impl, sleep_dur, alloc]() {
    auto* hli = impl_;
    while (!hli->stop_event.is_set()) {
      // Check if a packet is available.
      bool has_next;
      a0_subscriber_sync_has_next(&hli->sub, &has_next);

      // If not available, then either the heartbeat hasn't started, or has heartbeat is gone.
      // It's important that the listener's min_freq is less than the heartbeat's freq accounting
      // for scheduling noise.
      if (!has_next) {
        if (!hli->detected) {
          hli->stop_event.wait_for(sleep_dur);
          continue;
        }

        if (hli->onmissed.fn) {
          hli->onmissed.fn(hli->onmissed.user_data);
        }
        break;
      }

      // Get the packet.
      a0::scope<a0_packet_t> pkt({}, [&](a0_packet_t* pkt_) {
        a0_packet_dealloc(*pkt_, alloc);
      });
      a0_subscriber_sync_next(&hli->sub, pkt.get());

      uint64_t pkt_ts = 0;
      for (size_t i = 0; i < pkt->headers_block.size; i++) {
        if (!strcmp(pkt->headers_block.headers[i].key, A0_TIME_MONO)) {
          a0_time_mono_parse(pkt->headers_block.headers[i].val, &pkt_ts);
          break;
        }
      }
      if (!pkt_ts) {
        // Something has gone wrong and the timestamp is missing.
        // Maybe something other than heartbeat published on this topic?
        // TODO(lshamis): Figure out how to handle this case.
        //                For now, let this get interpreted as a very old packet.
        //                Don't trigger detection and do trigger missed.
      }

      uint64_t now_ts;
      a0_time_mono_now(&now_ts);

      if (!hli->detected) {
        if (now_ts < pkt_ts + (uint64_t)sleep_dur.count()) {
          hli->detected = true;
          if (hli->ondetected.fn) {
            hli->ondetected.fn(hli->ondetected.user_data);
          }
          a0_time_mono_now(&now_ts);
        } else {
          hli->stop_event.wait_for(sleep_dur);
          continue;
        }
      }

      if (now_ts < pkt_ts + sleep_dur.count()) {
        hli->stop_event.wait_for(sleep_dur - std::chrono::nanoseconds(now_ts - pkt_ts));
      }
    }
  });
  hl->_impl->init_event.set();

  return A0_OK;
}

errno_t a0_heartbeat_listener_close(a0_heartbeat_listener_t* hl) {
  if (!hl->_impl) {
    return ESHUTDOWN;
  }

  hl->_impl->stop_event.set();
  hl->_impl->thrd.join();
  a0_subscriber_sync_close(&hl->_impl->sub);

  delete hl->_impl;
  hl->_impl = nullptr;

  return A0_OK;
}

errno_t a0_heartbeat_listener_async_close(a0_heartbeat_listener_t* hl, a0_callback_t cb) {
  if (!hl->_impl) {
    return ESHUTDOWN;
  }

  hl->_impl->init_event.wait();
  hl->_impl->stop_event.set();
  std::thread t([hli = hl->_impl, cb]() {
    hli->thrd.join();
    a0_subscriber_sync_close(&hli->sub);
    delete hli;
    cb.fn(cb.user_data);
  });
  t.detach();

  hl->_impl = nullptr;
  return A0_OK;
}
