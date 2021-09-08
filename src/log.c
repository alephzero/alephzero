#include <a0/alloc.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/log.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/writer.h>

#include <stdlib.h>
#include <string.h>

#include "err_macro.h"
#include "topic.h"

A0_STATIC_INLINE
a0_err_t a0_log_topic_open(a0_log_topic_t topic, a0_file_t* file) {
  const char* tmpl = getenv("A0_LOG_TOPIC_TEMPLATE");
  if (!tmpl) {
    tmpl = "alephzero/{topic}.log.a0";
  }
  return a0_topic_open(tmpl, topic.name, topic.file_opts, file);
}

static const char LOG_LEVEL[] = "a0_log_level";

A0_STATIC_INLINE
const char* a0_log_level_name(a0_log_level_t level) {
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
a0_log_level_t a0_log_level_from_name(const char* name) {
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

a0_err_t a0_logger_init(a0_logger_t* logger, a0_log_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_log_topic_open(topic, &logger->_file));

  a0_err_t err = a0_writer_init(&logger->_writer, logger->_file.arena);
  if (err) {
    a0_file_close(&logger->_file);
    return err;
  }

  err = a0_writer_push(&logger->_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&logger->_writer);
    a0_file_close(&logger->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_logger_close(a0_logger_t* logger) {
  a0_writer_close(&logger->_writer);
  a0_file_close(&logger->_file);
  return A0_OK;
}

a0_err_t a0_logger_log(a0_logger_t* logger, a0_log_level_t level, a0_packet_t pkt) {
  const size_t num_extra_headers = 1;
  a0_packet_header_t extra_headers[] = {
      {LOG_LEVEL, a0_log_level_name(level)},
  };

  a0_packet_t full_pkt = pkt;
  full_pkt.headers_block = (a0_packet_headers_block_t){
      .headers = extra_headers,
      .size = num_extra_headers,
      .next_block = (a0_packet_headers_block_t*)&pkt.headers_block,
  };

  return a0_writer_write(&logger->_writer, full_pkt);
}

a0_err_t a0_logger_crit(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_CRIT, pkt);
}

a0_err_t a0_logger_err(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_ERR, pkt);
}

a0_err_t a0_logger_warn(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_WARN, pkt);
}

a0_err_t a0_logger_info(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_INFO, pkt);
}

a0_err_t a0_logger_dbg(a0_logger_t* logger, a0_packet_t pkt) {
  return a0_logger_log(logger, A0_LOG_LEVEL_DBG, pkt);
}

A0_STATIC_INLINE
void a0_log_listener_callback(void* data, a0_packet_t pkt) {
  a0_log_listener_t* log_list = (a0_log_listener_t*)data;

  a0_packet_header_iterator_t hdr_iter;
  a0_packet_header_iterator_init(&hdr_iter, &pkt);
  a0_packet_header_t lvl_hdr;
  if (a0_packet_header_iterator_next_match(&hdr_iter, LOG_LEVEL, &lvl_hdr)) {
    return;
  }
  a0_log_level_t level = a0_log_level_from_name(lvl_hdr.val);
  if (level <= log_list->_level) {
    a0_packet_callback_call(log_list->_onmsg, pkt);
  }
}

a0_err_t a0_log_listener_init(a0_log_listener_t* log_list,
                              a0_log_topic_t topic,
                              a0_alloc_t alloc,
                              a0_log_level_t level,
                              a0_packet_callback_t onmsg) {
  log_list->_level = level;
  log_list->_onmsg = onmsg;
  A0_RETURN_ERR_ON_ERR(a0_log_topic_open(topic, &log_list->_file));

  a0_err_t err = a0_reader_init(
      &log_list->_reader,
      log_list->_file.arena,
      alloc,
      A0_INIT_AWAIT_NEW,
      A0_ITER_NEXT,
      (a0_packet_callback_t){log_list, a0_log_listener_callback});
  if (err) {
    a0_file_close(&log_list->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_log_listener_close(a0_log_listener_t* log_list) {
  a0_reader_close(&log_list->_reader);
  a0_file_close(&log_list->_file);
  return A0_OK;
}
