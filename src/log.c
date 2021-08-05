#include <a0/file.h>
#include <a0/log.h>
#include <a0/reader.h>
#include <a0/writer.h>
#include <a0/writer_middleware.h>

#include <stdlib.h>

#include "protocol_util.h"

A0_STATIC_INLINE
errno_t _a0_log_open_topic(a0_log_topic_t topic, a0_file_t* file) {
  const char* template = getenv("A0_LOG_TOPIC_TEMPLATE");
  if (!template) {
    template = "alephzero/{topic}.log.a0";
  }
  return a0_open_topic(template, topic.name, topic.file_opts, file);
}

static const char LOG_LEVEL[] = "a0_level";

A0_STATIC_INLINE
const char* _a0_log_level_name(a0_log_level_t level) {
  switch (level) {
    case A0_LOG_LEVEL_CRIT: {
      return "CRIT";
    }
    case A0_LOG_LEVEL_ERR: {
      return "ERR";
    }
    case A0_LOG_LEVEL_WARN: {
      return "WARN";
    }
    case A0_LOG_LEVEL_INFO: {
      return "INFO";
    }
    case A0_LOG_LEVEL_DBG: {
      return "DBG";
    }
    default: {
      return "UNKNOWN";
    }
  }
}

A0_STATIC_INLINE
a0_log_level_t _a0_log_level_from_name(const char* name) {
  if (!strcmp(name, "CRIT")) {
    return A0_LOG_LEVEL_CRIT;
  } else if (!strcmp(name, "ERR")) {
    return A0_LOG_LEVEL_ERR;
  } else if (!strcmp(name, "WARN")) {
    return A0_LOG_LEVEL_WARN;
  } else if (!strcmp(name, "INFO")) {
    return A0_LOG_LEVEL_INFO;
  } else if (!strcmp(name, "DBG")) {
    return A0_LOG_LEVEL_DBG;
  } else {
    return A0_LOG_LEVEL_UNKNOWN;
  }
}

errno_t a0_logger_init(a0_logger_t* logger, a0_log_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(_a0_log_open_topic(topic, &logger->_file));

  errno_t err = a0_writer_init(&logger->_writer, logger->_file.arena);
  if (err) {
    a0_file_close(&logger->_file);
    return err;
  }

  err = a0_writer_push(
      &logger->_writer,
      a0_writer_middleware_add_standard_headers());
  if (err) {
    a0_writer_close(&logger->_writer);
    a0_file_close(&logger->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_logger_close(a0_logger_t* logger) {
  a0_writer_close(&logger->_writer);
  a0_file_close(&logger->_file);
  return A0_OK;
}

errno_t a0_logger_log(a0_logger_t* logger, a0_log_level_t level, a0_packet_t pkt) {
  const size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[] = {
      {LOG_LEVEL, _a0_log_level_name(level)},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_writer_write(&logger->_writer, full_pkt);
}

errno_t a0_logger_crit(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_CRIT, pkt);
}

errno_t a0_logger_err(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_ERR, pkt);
}

errno_t a0_logger_warn(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_WARN, pkt);
}

errno_t a0_logger_info(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_INFO, pkt);
}

errno_t a0_logger_dbg(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_DBG, pkt);
}

A0_STATIC_INLINE
void _a0_log_listener_callback(void* data, a0_packet_t pkt) {
  a0_log_listener_t* log_list = (a0_log_listener_t*)data;
  const char* level_str;
  if (a0_find_header(pkt, LOG_LEVEL, &level_str)) {
    return;
  }
  a0_log_level_t level = _a0_log_level_from_name(level_str);
  if (level <= log_list->_level) {
    a0_packet_callback_call(log_list->_onmsg, pkt);
  }
}

errno_t a0_log_listener_init(a0_log_listener_t* log_list,
                             a0_log_topic_t topic,
                             a0_alloc_t alloc,
                             a0_log_level_t level,
                             a0_packet_callback_t onmsg) {
  log_list->_level = level;
  log_list->_onmsg = onmsg;
  A0_RETURN_ERR_ON_ERR(_a0_log_open_topic(topic, &log_list->_file));

  errno_t err = a0_reader_init(
      &log_list->_reader,
      log_list->_file.arena,
      alloc,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){log_list, _a0_log_listener_callback});
  if (err) {
    a0_file_close(&log_list->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_log_listener_close(a0_log_listener_t* log_list) {
  a0_reader_close(&log_list->_reader);
  a0_file_close(&log_list->_file);
  return A0_OK;
}
