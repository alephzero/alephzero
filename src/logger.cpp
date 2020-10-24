#include <a0/arena.h>
#include <a0/errno.h>
#include <a0/logger.h>
#include <a0/packet.h>
#include <a0/pubsub.h>

#include <cerrno>

struct a0_logger_impl_s {
  a0_publisher_t publisher_crit;
  a0_publisher_t publisher_err;
  a0_publisher_t publisher_warn;
  a0_publisher_t publisher_info;
  a0_publisher_t publisher_dbg;
};

errno_t a0_logger_init(a0_logger_t* log,
                       a0_arena_t arena_crit,
                       a0_arena_t arena_err,
                       a0_arena_t arena_warn,
                       a0_arena_t arena_info,
                       a0_arena_t arena_dbg) {
  log->_impl = new a0_logger_impl_t;

  a0_publisher_init(&log->_impl->publisher_crit, arena_crit);
  a0_publisher_init(&log->_impl->publisher_err, arena_err);
  a0_publisher_init(&log->_impl->publisher_warn, arena_warn);
  a0_publisher_init(&log->_impl->publisher_info, arena_info);
  a0_publisher_init(&log->_impl->publisher_dbg, arena_dbg);

  return A0_OK;
}

errno_t a0_logger_close(a0_logger_t* log) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }

  a0_publisher_close(&log->_impl->publisher_crit);
  a0_publisher_close(&log->_impl->publisher_err);
  a0_publisher_close(&log->_impl->publisher_warn);
  a0_publisher_close(&log->_impl->publisher_info);
  a0_publisher_close(&log->_impl->publisher_dbg);
  delete log->_impl;
  log->_impl = nullptr;

  return A0_OK;
}

errno_t a0_log_crit(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }
  return a0_pub(&log->_impl->publisher_crit, pkt);
}
errno_t a0_log_err(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }
  return a0_pub(&log->_impl->publisher_err, pkt);
}
errno_t a0_log_warn(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }
  return a0_pub(&log->_impl->publisher_warn, pkt);
}
errno_t a0_log_info(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }
  return a0_pub(&log->_impl->publisher_info, pkt);
}
errno_t a0_log_dbg(a0_logger_t* log, const a0_packet_t pkt) {
  if (!log->_impl) {
    return ESHUTDOWN;
  }
  return a0_pub(&log->_impl->publisher_dbg, pkt);
}
