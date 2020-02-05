#ifndef A0_LOGGER_H
#define A0_LOGGER_H

#include <a0/common.h>
#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_logger_impl_s a0_logger_impl_t;

typedef struct a0_logger_s {
  a0_logger_impl_t* _impl;
} a0_logger_t;

errno_t a0_logger_init(a0_logger_t*,
                       a0_buf_t arena_crit,
                       a0_buf_t arena_err,
                       a0_buf_t arena_warn,
                       a0_buf_t arena_info,
                       a0_buf_t arena_dgb);
errno_t a0_logger_close(a0_logger_t*);
errno_t a0_log_crit(a0_logger_t*, const a0_packet_t);
errno_t a0_log_err(a0_logger_t*, const a0_packet_t);
errno_t a0_log_warn(a0_logger_t*, const a0_packet_t);
errno_t a0_log_info(a0_logger_t*, const a0_packet_t);
errno_t a0_log_dbg(a0_logger_t*, const a0_packet_t);

#ifdef __cplusplus
}
#endif

#endif  // A0_LOGGER_H
