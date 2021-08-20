#include <a0/callback.h>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/file.h>
#include <a0/heartbeat.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/time.h>
#include <a0/transport.h>
#include <a0/writer.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "empty.h"
#include "err_macro.h"
#include "protocol_util.h"

A0_STATIC_INLINE
errno_t _a0_heartbeat_open_topic(a0_heartbeat_topic_t topic, a0_file_t* file) {
  const char* template = getenv("A0_HEARTBEAT_TOPIC_TEMPLATE");
  if (!template) {
    template = "alephzero/{topic}.heartbeat.a0";
  }
  return a0_open_topic(template, topic.name, topic.file_opts, file);
}

const a0_heartbeat_options_t A0_HEARTBEAT_OPTIONS_DEFAULT = {
    .freq = 10,
};

A0_STATIC_INLINE
void* a0_heartbeat_thread_main(void* data) {
  a0_heartbeat_t* h = (a0_heartbeat_t*)data;

  const uint64_t sleep_ns = NS_PER_SEC / h->_opts.freq;

  a0_time_mono_t wake_time;
  a0_time_mono_now(&wake_time);

  while (!a0_event_is_set(&h->_stop_event)) {
    a0_packet_t pkt;
    a0_packet_init(&pkt);
    a0_writer_write(&h->_annotated_writer, pkt);

    a0_time_mono_add(wake_time, sleep_ns, &wake_time);
    a0_event_timedwait(&h->_stop_event, wake_time);
  }

  return NULL;
}

errno_t a0_heartbeat_init(a0_heartbeat_t* h,
                          a0_heartbeat_topic_t topic,
                          const a0_heartbeat_options_t* opts) {
  h->_opts = A0_HEARTBEAT_OPTIONS_DEFAULT;
  if (opts) {
    h->_opts = *opts;
  }

  A0_RETURN_ERR_ON_ERR(_a0_heartbeat_open_topic(topic, &h->_file));

  errno_t err = a0_writer_init(&h->_simple_writer, h->_file.arena);
  if (err) {
    a0_file_close(&h->_file);
    return err;
  }

  err = a0_writer_wrap(
      &h->_simple_writer,
      a0_add_standard_headers(),
      &h->_annotated_writer);
  if (err) {
    a0_writer_close(&h->_simple_writer);
    a0_file_close(&h->_file);
    return err;
  }

  a0_event_init(&h->_stop_event);

  pthread_create(
      &h->_thread,
      NULL,
      a0_heartbeat_thread_main,
      h);

  return A0_OK;
}

errno_t a0_heartbeat_close(a0_heartbeat_t* h) {
  a0_event_set(&h->_stop_event);
  pthread_join(h->_thread, NULL);

  a0_writer_close(&h->_annotated_writer);
  a0_writer_close(&h->_simple_writer);
  a0_file_close(&h->_file);
  a0_event_close(&h->_stop_event);

  return A0_OK;
}

const a0_heartbeat_listener_options_t A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT = {
    .min_freq = 5,
};

A0_STATIC_INLINE
a0_time_mono_t a0_flat_packet_time_mono(a0_flat_packet_t fpkt) {
  a0_packet_stats_t stats;
  a0_flat_packet_stats(fpkt, &stats);

  a0_time_mono_t time_mono = A0_EMPTY;
  for (size_t i = 0; i < stats.num_hdrs; i++) {
    a0_packet_header_t hdr;
    a0_flat_packet_header(fpkt, i, &hdr);
    if (!strcmp(hdr.key, A0_TIME_MONO)) {
      a0_time_mono_parse(hdr.val, &time_mono);
      break;
    }
  }
  return time_mono;
}

A0_STATIC_INLINE
void* a0_heartbeat_listener_thread_main(void* data) {
  a0_heartbeat_listener_t* hl = (a0_heartbeat_listener_t*)data;

  const uint64_t sleep_ns = NS_PER_SEC / hl->_opts.min_freq;

  a0_locked_transport_t tlk;
  a0_transport_lock(&hl->_transport, &tlk);
  a0_transport_wait(tlk, a0_transport_has_next_pred(&tlk));

  bool shutdown_requested;
  a0_transport_shutdown_requested(tlk, &shutdown_requested);

  if (shutdown_requested) {
    a0_transport_unlock(tlk);
    return NULL;
  }

  a0_transport_unlock(tlk);

  a0_callback_call(hl->ondetected);

  a0_transport_lock(&hl->_transport, &tlk);

  while (true) {
    a0_transport_shutdown_requested(tlk, &shutdown_requested);
    if (shutdown_requested) {
      a0_transport_unlock(tlk);
      return NULL;
    }

    bool has_next;
    a0_transport_has_next(tlk, &has_next);
    if (!has_next) {
      a0_transport_unlock(tlk);
      a0_callback_call(hl->onmissed);
      return NULL;
    }

    a0_transport_step_next(tlk);

    a0_transport_frame_t frame;
    a0_transport_frame(tlk, &frame);

    // It's faster not to copy the packet out.
    a0_flat_packet_t fpkt = {
        .ptr = frame.data,
        .size = frame.hdr.data_size,
    };

    // TODO(lshamis): remove this system call from the critical section.
    a0_time_mono_t now_ts;
    a0_time_mono_now(&now_ts);
    int64_t now_ns = now_ts.ts.tv_sec * NS_PER_SEC + now_ts.ts.tv_nsec;

    a0_time_mono_t wake_ts;
    a0_time_mono_add(a0_flat_packet_time_mono(fpkt), sleep_ns, &wake_ts);
    int64_t wake_ns = wake_ts.ts.tv_sec * NS_PER_SEC + wake_ts.ts.tv_nsec;

    if (now_ns < wake_ns) {
      a0_transport_timedwait(
          tlk, a0_transport_has_next_pred(&tlk), wake_ts);
    }
  }

  return NULL;
}

errno_t a0_heartbeat_listener_init(a0_heartbeat_listener_t* hl,
                                   a0_heartbeat_topic_t topic,
                                   const a0_heartbeat_listener_options_t* opts,
                                   a0_callback_t ondetected,
                                   a0_callback_t onmissed) {
  hl->_opts = A0_HEARTBEAT_LISTENER_OPTIONS_DEFAULT;
  if (opts) {
    hl->_opts = *opts;
  }

  hl->ondetected = ondetected;
  hl->onmissed = onmissed;

  A0_RETURN_ERR_ON_ERR(_a0_heartbeat_open_topic(topic, &hl->_file));

  errno_t err = a0_transport_init(&hl->_transport, hl->_file.arena);
  if (err) {
    a0_file_close(&hl->_file);
    return err;
  }
  a0_locked_transport_t tlk;
  a0_transport_lock(&hl->_transport, &tlk);
  a0_transport_jump_tail(tlk);
  a0_transport_unlock(tlk);

  pthread_create(
      &hl->_thread,
      NULL,
      a0_heartbeat_listener_thread_main,
      hl);

  return A0_OK;
}

errno_t a0_heartbeat_listener_close(a0_heartbeat_listener_t* hl) {
  a0_locked_transport_t tlk;
  a0_transport_lock(&hl->_transport, &tlk);
  a0_transport_shutdown(tlk);
  a0_transport_unlock(tlk);

  pthread_join(hl->_thread, NULL);

  a0_file_close(&hl->_file);

  return A0_OK;
}
