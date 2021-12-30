#ifndef A0_LOG_H
#define A0_LOG_H

#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum a0_log_level_e {
  A0_LOG_LEVEL_CRIT = 0,
  A0_LOG_LEVEL_ERR = 1,
  A0_LOG_LEVEL_WARN = 2,
  A0_LOG_LEVEL_INFO = 3,
  A0_LOG_LEVEL_DBG = 4,
  A0_LOG_LEVEL_MIN = A0_LOG_LEVEL_CRIT,
  A0_LOG_LEVEL_MAX = A0_LOG_LEVEL_DBG,
  A0_LOG_LEVEL_UNKNOWN = A0_LOG_LEVEL_MAX + 1,
} a0_log_level_t;

typedef struct a0_log_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_log_topic_t;

typedef struct a0_logger_s {
  a0_file_t _file;
  a0_writer_t _writer;
} a0_logger_t;

a0_err_t a0_logger_init(a0_logger_t*, a0_log_topic_t);
a0_err_t a0_logger_close(a0_logger_t*);

a0_err_t a0_logger_log(a0_logger_t*, a0_log_level_t, a0_packet_t);
a0_err_t a0_logger_crit(a0_logger_t*, a0_packet_t);
a0_err_t a0_logger_err(a0_logger_t*, a0_packet_t);
a0_err_t a0_logger_warn(a0_logger_t*, a0_packet_t);
a0_err_t a0_logger_info(a0_logger_t*, a0_packet_t);
a0_err_t a0_logger_dbg(a0_logger_t*, a0_packet_t);

typedef struct a0_log_listener_s {
  a0_file_t _file;
  a0_reader_t _reader;
  a0_log_level_t _level;
  a0_packet_callback_t _onmsg;
} a0_log_listener_t;

a0_err_t a0_log_listener_init(a0_log_listener_t*,
                              a0_log_topic_t,
                              a0_alloc_t,
                              a0_log_level_t,
                              a0_reader_options_t,
                              a0_packet_callback_t);
a0_err_t a0_log_listener_close(a0_log_listener_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_LOG_H
