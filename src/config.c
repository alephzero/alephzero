#include <a0/alloc.h>
#include <a0/config.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/writer.h>

#include <stdlib.h>

#include "err_macro.h"
#include "topic.h"

A0_STATIC_INLINE
a0_err_t _a0_config_topic_open(a0_config_topic_t topic, a0_file_t* out) {
  const char* template = getenv("A0_CONFIG_TOPIC_TEMPLATE");
  if (!template) {
    template = "alephzero/{topic}.cfg.a0";
  }
  return a0_topic_open(template, topic.name, topic.file_opts, out);
}

a0_err_t a0_config(a0_config_topic_t topic,
                   a0_alloc_t alloc,
                   int flags,
                   a0_packet_t* out) {
  a0_file_t file;
  A0_RETURN_ERR_ON_ERR(_a0_config_topic_open(topic, &file));

  a0_err_t err = a0_reader_read_one(file.arena,
                                    alloc,
                                    A0_INIT_MOST_RECENT,
                                    flags,
                                    out);

  a0_file_close(&file);
  return err;
}

a0_err_t a0_write_config(a0_config_topic_t topic, a0_packet_t pkt) {
  a0_file_t file;
  A0_RETURN_ERR_ON_ERR(_a0_config_topic_open(topic, &file));

  a0_writer_t writer;
  a0_err_t err = a0_writer_init(&writer, file.arena);
  if (err) {
    a0_file_close(&file);
    return err;
  }

  err = a0_writer_push(&writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&writer);
    a0_file_close(&file);
    return err;
  }

  err = a0_writer_write(&writer, pkt);

  a0_writer_close(&writer);
  a0_file_close(&file);
  return err;
}

a0_err_t a0_onconfig_init(a0_onconfig_t* cfg,
                          a0_config_topic_t topic,
                          a0_alloc_t alloc,
                          a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(_a0_config_topic_open(topic, &cfg->_file));

  a0_err_t err = a0_reader_init(
      &cfg->_reader,
      cfg->_file.arena,
      alloc,
      A0_INIT_MOST_RECENT,
      A0_ITER_NEWEST,
      onpacket);
  if (err) {
    a0_file_close(&cfg->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_onconfig_close(a0_onconfig_t* cfg) {
  a0_reader_close(&cfg->_reader);
  a0_file_close(&cfg->_file);
  return A0_OK;
}
