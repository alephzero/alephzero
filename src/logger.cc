#include <a0/common.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/stream.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "macros.h"
#include "packet_tools.h"
#include "stream_tools.hpp"

A0_STATIC_INLINE
a0_stream_protocol_t protocol_info() {
  static a0_stream_protocol_t protocol = []() {
    static const char kProtocolName[] = "a0_logger";

    a0_stream_protocol_t p;
    p.name.size = sizeof(kProtocolName);
    p.name.ptr = (uint8_t*)kProtocolName;

    p.major_version = 0;
    p.minor_version = 1;
    p.patch_version = 0;

    p.metadata_size = 0;

    return p;
  }();

  return protocol;
}

struct a0_logger_impl_s {
  a0_stream_t stream_crit;
  a0_stream_t stream_err;
  a0_stream_t stream_warn;
  a0_stream_t stream_info;
  a0_stream_t stream_dbg;
};

errno_t a0_logger_init(a0_logger_t* log,
                       a0_buf_t arena_crit,
                       a0_buf_t arena_err,
                       a0_buf_t arena_warn,
                       a0_buf_t arena_info,
                       a0_buf_t arena_dbg) {
  log->_impl = new a0_logger_impl_t;

  auto init_stream = [](a0_stream_t* stream, a0_buf_t arena) {
    a0_stream_init_status_t init_status;
    a0_locked_stream_t slk;
    a0_stream_init(stream, arena, protocol_info(), &init_status, &slk);

    if (init_status == A0_STREAM_CREATED) {
      // TODO: Add metadata...
    }

    a0_unlock_stream(slk);

    if (init_status == A0_STREAM_PROTOCOL_MISMATCH) {
      // TODO: Report error?
    }
  };

  init_stream(&log->_impl->stream_crit, arena_crit);
  init_stream(&log->_impl->stream_err, arena_err);
  init_stream(&log->_impl->stream_warn, arena_warn);
  init_stream(&log->_impl->stream_info, arena_info);
  init_stream(&log->_impl->stream_dbg, arena_dbg);

  return A0_OK;
}

errno_t a0_logger_close(a0_logger_t* log) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }

  a0_stream_close(&log->_impl->stream_crit);
  a0_stream_close(&log->_impl->stream_err);
  a0_stream_close(&log->_impl->stream_warn);
  a0_stream_close(&log->_impl->stream_info);
  a0_stream_close(&log->_impl->stream_dbg);
  delete log->_impl;
  log->_impl = nullptr;

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_log(a0_stream_t* log_stream, const a0_packet_t pkt) {
  char mono_str[20];
  char wall_str[36];
  a0::time_strings(mono_str, wall_str);

  constexpr size_t num_extra_headers = 2;
  a0_packet_header_t extra_headers[num_extra_headers] = {
      {kMonoTime, mono_str},
      {kWallTime, wall_str},
  };

  // TODO: Add sequence numbers.

  a0::sync_stream_t ss{log_stream};
  return ss.with_lock([&](a0_locked_stream_t slk) {
    a0_packet_copy_with_additional_headers({extra_headers, num_extra_headers, nullptr},
                                           pkt,
                                           a0::stream_allocator(&slk),
                                           nullptr);
    return a0_stream_commit(slk);
  });
}

errno_t a0_log_crit(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log || !log->_impl) {
    return ESHUTDOWN;
  }
  return a0_log(&log->_impl->stream_crit, pkt);
}
errno_t a0_log_err(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log || !log->_impl) {
    return ESHUTDOWN;
  }
  return a0_log(&log->_impl->stream_err, pkt);
}
errno_t a0_log_warn(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log || !log->_impl) {
    return ESHUTDOWN;
  }
  return a0_log(&log->_impl->stream_warn, pkt);
}
errno_t a0_log_info(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log || !log->_impl) {
    return ESHUTDOWN;
  }
  return a0_log(&log->_impl->stream_info, pkt);
}
errno_t a0_log_dbg(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log || !log->_impl) {
    return ESHUTDOWN;
  }
  return a0_log(&log->_impl->stream_dbg, pkt);
}
