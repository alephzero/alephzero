#include <a0/alloc.h>
#include <a0/cfg.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/writer.h>

#include <stdbool.h>
#include <stddef.h>

#include "err_macro.h"

A0_STATIC_INLINE
a0_err_t a0_cfg_topic_open(a0_cfg_topic_t topic, a0_file_t* out) {
  return a0_topic_open(a0_env_topic_tmpl_cfg(), topic.name, topic.file_opts, out);
}

a0_err_t a0_cfg_init(a0_cfg_t* cfg, a0_cfg_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_cfg_topic_open(topic, &cfg->_file));

  a0_err_t err = a0_writer_init(&cfg->_writer, cfg->_file.arena);
  if (err) {
    a0_file_close(&cfg->_file);
    return err;
  }

  err = a0_writer_push(&cfg->_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&cfg->_writer);
    a0_file_close(&cfg->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_cfg_close(a0_cfg_t* cfg) {
  a0_writer_close(&cfg->_writer);
  a0_file_close(&cfg->_file);
  return A0_OK;
}

a0_err_t a0_cfg_read(a0_cfg_t* cfg,
                     a0_alloc_t alloc,
                     a0_packet_t* out) {
  a0_reader_sync_t reader_sync;
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, cfg->_file.arena, alloc, (a0_reader_options_t){A0_INIT_MOST_RECENT, A0_ITER_NEXT}));
  a0_err_t err = a0_reader_sync_read(&reader_sync, out);
  a0_reader_sync_close(&reader_sync);
  return err;
}

a0_err_t a0_cfg_read_blocking(a0_cfg_t* cfg,
                              a0_alloc_t alloc,
                              a0_packet_t* out) {
  return a0_cfg_read_blocking_timeout(cfg, alloc, A0_TIMEOUT_NEVER, out);
}

a0_err_t a0_cfg_read_blocking_timeout(a0_cfg_t* cfg,
                                      a0_alloc_t alloc,
                                      a0_time_mono_t* timeout,
                                      a0_packet_t* out) {
  a0_reader_sync_t reader_sync;
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, cfg->_file.arena, alloc, (a0_reader_options_t){A0_INIT_MOST_RECENT, A0_ITER_NEXT}));
  a0_err_t err = a0_reader_sync_read_blocking_timeout(&reader_sync, timeout, out);
  a0_reader_sync_close(&reader_sync);
  return err;
}

a0_err_t a0_cfg_write(a0_cfg_t* cfg, a0_packet_t pkt) {
  return a0_writer_write(&cfg->_writer, pkt);
}

// NOLINTNEXTLINE(readability-non-const-parameter): written cannot be const.
a0_err_t a0_cfg_write_if_empty(a0_cfg_t* cfg, a0_packet_t pkt, bool* written) {
  bool unused;
  if (!written) {
    written = &unused;
  }

  a0_writer_t write_if_empty_writer;
  a0_writer_wrap(
      &cfg->_writer,
      a0_write_if_empty(written),
      &write_if_empty_writer);

  a0_err_t err = a0_writer_write(&write_if_empty_writer, pkt);

  a0_writer_close(&write_if_empty_writer);
  return err;
}

a0_err_t a0_cfg_mergepatch(a0_cfg_t* cfg, a0_packet_t pkt) {
  a0_writer_t w;
  a0_writer_wrap(
      &cfg->_writer,
      a0_json_mergepatch(),
      &w);
  a0_err_t err = a0_writer_write(&w, pkt);
  a0_writer_close(&w);
  return err;
}

a0_err_t a0_cfg_watcher_init(a0_cfg_watcher_t* cw,
                             a0_cfg_topic_t topic,
                             a0_alloc_t alloc,
                             a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_cfg_topic_open(topic, &cw->_file));

  a0_err_t err = a0_reader_init(
      &cw->_reader,
      cw->_file.arena,
      alloc,
      (a0_reader_options_t){A0_INIT_MOST_RECENT, A0_ITER_NEWEST},
      onpacket);
  if (err) {
    a0_file_close(&cw->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_cfg_watcher_close(a0_cfg_watcher_t* cw) {
  a0_reader_close(&cw->_reader);
  a0_file_close(&cw->_file);
  return A0_OK;
}
