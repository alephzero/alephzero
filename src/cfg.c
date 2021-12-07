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
#include <a0/transport.h>
#include <a0/writer.h>

#include <stdbool.h>
#include <stddef.h>

#include "err_macro.h"

#ifdef A0_EXT_YYJSON

#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/unused.h>

#include <alloca.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <yyjson.h>

#endif  // A0_EXT_YYJSON

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
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, cfg->_file.arena, alloc, A0_INIT_MOST_RECENT, A0_ITER_NEXT));
  a0_err_t err = a0_reader_sync_read(&reader_sync, out);
  a0_reader_sync_close(&reader_sync);
  return err;
}

a0_err_t a0_cfg_read_blocking(a0_cfg_t* cfg,
                              a0_alloc_t alloc,
                              a0_packet_t* out) {
  a0_reader_sync_t reader_sync;
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, cfg->_file.arena, alloc, A0_INIT_MOST_RECENT, A0_ITER_NEXT));
  a0_err_t err = a0_reader_sync_read_blocking(&reader_sync, out);
  a0_reader_sync_close(&reader_sync);
  return err;
}

a0_err_t a0_cfg_read_blocking_timeout(a0_cfg_t* cfg,
                                      a0_alloc_t alloc,
                                      a0_time_mono_t timeout,
                                      a0_packet_t* out) {
  a0_reader_sync_t reader_sync;
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&reader_sync, cfg->_file.arena, alloc, A0_INIT_MOST_RECENT, A0_ITER_NEXT));
  a0_err_t err = a0_reader_sync_read_blocking_timeout(&reader_sync, timeout, out);
  a0_reader_sync_close(&reader_sync);
  return err;
}

a0_err_t a0_cfg_write(a0_cfg_t* cfg, a0_packet_t pkt) {
  return a0_writer_write(&cfg->_writer, pkt);
}

A0_STATIC_INLINE
a0_err_t a0_cfg_write_if_empty_process_locked(
    void* user_data,
    a0_transport_writer_locked_t* twl,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  bool* empty = (bool*)user_data;
  a0_transport_writer_empty(twl, empty);
  if (!*empty) {
    a0_transport_writer_unlock(twl);
    return A0_OK;
  }
  return a0_middleware_chain(chain, pkt);
}

// NOLINTNEXTLINE(readability-non-const-parameter): written cannot be const.
a0_err_t a0_cfg_write_if_empty(a0_cfg_t* cfg, a0_packet_t pkt, bool* written) {
  bool unused;
  if (!written) {
    written = &unused;
  }

  a0_middleware_t write_if_empty_middleware = {
      .user_data = written,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_cfg_write_if_empty_process_locked,
  };

  a0_writer_t write_if_empty_writer;
  a0_writer_wrap(
      &cfg->_writer,
      write_if_empty_middleware,
      &write_if_empty_writer);

  a0_err_t err = a0_writer_write(&write_if_empty_writer, pkt);

  a0_writer_close(&write_if_empty_writer);
  return err;
}

#ifdef A0_EXT_YYJSON

A0_STATIC_INLINE
size_t a0_yyjson_read_flags() {
  return YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INF_AND_NAN;
}

A0_STATIC_INLINE
size_t a0_yyjson_write_flags() {
  return YYJSON_WRITE_ESCAPE_UNICODE | YYJSON_WRITE_ESCAPE_SLASHES;
}

typedef struct a0_yyjson_alloc_s {
  a0_alloc_t alloc;
  a0_buf_t pkt_buf;
  a0_buf_t yyjson_buf;
} a0_yyjson_alloc_t;

A0_STATIC_INLINE
a0_err_t yyjson_alloc_wrapper(void* user_data, size_t size, a0_buf_t* out) {
  a0_yyjson_alloc_t* yyjson_alloc = (a0_yyjson_alloc_t*)user_data;

  size_t yyjson_size = yyjson_read_max_memory_usage(
      size, a0_yyjson_read_flags());

  a0_buf_t buf;
  a0_err_t err = a0_alloc(yyjson_alloc->alloc, size + yyjson_size, &buf);

  *out = (a0_buf_t){buf.data, size};
  yyjson_alloc->pkt_buf = *out;
  yyjson_alloc->yyjson_buf = (a0_buf_t){buf.data + size, yyjson_size};
  return err;
}

typedef struct a0_cfg_read_yyjson_action_s {
  void* user_data;
  a0_err_t (*fn)(void* user_data, a0_cfg_t*, a0_alloc_t, a0_packet_t*);
} a0_cfg_read_yyjson_action_t;

A0_STATIC_INLINE
a0_err_t a0_cfg_read_yyjson_helper(a0_cfg_t* cfg,
                                   a0_alloc_t alloc,
                                   yyjson_doc* out,
                                   a0_cfg_read_yyjson_action_t action) {
  a0_yyjson_alloc_t yyjson_alloc = {alloc, A0_EMPTY, A0_EMPTY};

  a0_alloc_t alloc_wrapper = {
      .user_data = &yyjson_alloc,
      .alloc = yyjson_alloc_wrapper,
      .dealloc = alloc.dealloc,
  };

  a0_packet_t pkt;
  A0_RETURN_ERR_ON_ERR(action.fn(action.user_data, cfg, alloc_wrapper, &pkt));

  yyjson_alc alc;
  yyjson_alc_pool_init(
      &alc,
      yyjson_alloc.yyjson_buf.data,
      yyjson_alloc.yyjson_buf.size);

  yyjson_read_err read_err;
  yyjson_doc* result = yyjson_read_opts(
      (char*)pkt.payload.data,
      pkt.payload.size,
      a0_yyjson_read_flags(),
      &alc,
      &read_err);
  if (read_err.code) {
    return A0_MAKE_MSGERR("Failed to parse cfg: %s", read_err.msg);
  }
  *out = *result;
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_cfg_read_yyjson_action(void* user_data, a0_cfg_t* cfg, a0_alloc_t alloc, a0_packet_t* out) {
  A0_MAYBE_UNUSED(user_data);
  return a0_cfg_read(cfg, alloc, out);
}

a0_err_t a0_cfg_read_yyjson(a0_cfg_t* cfg,
                            a0_alloc_t alloc,
                            yyjson_doc* out) {
  return a0_cfg_read_yyjson_helper(
      cfg, alloc, out, (a0_cfg_read_yyjson_action_t){NULL, a0_cfg_read_yyjson_action});
}

A0_STATIC_INLINE
a0_err_t a0_cfg_read_blocking_yyjson_action(void* user_data, a0_cfg_t* cfg, a0_alloc_t alloc, a0_packet_t* out) {
  A0_MAYBE_UNUSED(user_data);
  return a0_cfg_read_blocking(cfg, alloc, out);
}

a0_err_t a0_cfg_read_blocking_yyjson(a0_cfg_t* cfg,
                                     a0_alloc_t alloc,
                                     yyjson_doc* out) {
  return a0_cfg_read_yyjson_helper(
      cfg, alloc, out, (a0_cfg_read_yyjson_action_t){NULL, a0_cfg_read_blocking_yyjson_action});
}

A0_STATIC_INLINE
a0_err_t a0_cfg_read_blocking_timeout_yyjson_action(void* user_data, a0_cfg_t* cfg, a0_alloc_t alloc, a0_packet_t* out) {
  a0_time_mono_t* timeout = (a0_time_mono_t*)user_data;
  return a0_cfg_read_blocking_timeout(cfg, alloc, *timeout, out);
}

a0_err_t a0_cfg_read_blocking_timeout_yyjson(a0_cfg_t* cfg,
                                             a0_alloc_t alloc,
                                             a0_time_mono_t timeout,
                                             yyjson_doc* out) {
  return a0_cfg_read_yyjson_helper(
      cfg, alloc, out, (a0_cfg_read_yyjson_action_t){&timeout, a0_cfg_read_blocking_timeout_yyjson_action});
}

a0_err_t a0_cfg_write_yyjson(a0_cfg_t* cfg, yyjson_doc doc) {
  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_write_opts(
      &doc,
      a0_yyjson_write_flags(),
      NULL,  // TODO(lshamis): Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    return A0_MAKE_MSGERR("Failed to serialize cfg: %s", write_err.msg);
  }

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  pkt.payload = (a0_buf_t){(uint8_t*)data, size};

  a0_err_t err = a0_cfg_write(cfg, pkt);
  free(data);
  return err;
}

a0_err_t a0_cfg_write_if_empty_yyjson(a0_cfg_t* cfg, yyjson_doc doc, bool* written) {
  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_write_opts(
      &doc,
      a0_yyjson_write_flags(),
      NULL,  // TODO(lshamis): Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    return A0_MAKE_MSGERR("Failed to serialize cfg: %s", write_err.msg);
  }

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  pkt.payload = (a0_buf_t){(uint8_t*)data, size};

  a0_err_t err = a0_cfg_write_if_empty(cfg, pkt, written);
  free(data);
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mergepatch_process_locked_empty(
    yyjson_doc* mergepatch,
    a0_transport_writer_locked_t* twl,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(twl);

  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_write_opts(
      mergepatch,
      a0_yyjson_write_flags(),
      NULL,  // TODO(lshamis): Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    free(data);
    return A0_MAKE_MSGERR("Failed to serialize cfg: %s", write_err.msg);
  }

  pkt->payload = (a0_buf_t){(uint8_t*)data, size};

  a0_err_t err = a0_middleware_chain(chain, pkt);
  free(data);
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mergepatch_process_locked_nonempty(
    yyjson_doc* mergepatch,
    a0_transport_writer_locked_t* twl,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  a0_transport_reader_t tr;
  a0_transport_reader_locked_t trl;
  a0_transport_writer_as_reader(twl, &tr, &trl);

  a0_transport_reader_jump_tail(&trl);
  a0_transport_frame_t frame;
  a0_transport_reader_frame(&trl, &frame);

  a0_flat_packet_t flat_packet = {
      .buf = {frame.data, frame.hdr.data_size},
  };
  a0_buf_t old_payload;
  a0_flat_packet_payload(flat_packet, &old_payload);

  size_t yyjson_size = yyjson_read_max_memory_usage(
      old_payload.size, a0_yyjson_read_flags());
  assert(yyjson_size > 0);

  void* yyjson_data = alloca(yyjson_size);

  yyjson_alc alc;
  yyjson_alc_pool_init(
      &alc,
      yyjson_data,
      yyjson_size);

  yyjson_read_err read_err;
  yyjson_doc* original = yyjson_read_opts(
      (char*)old_payload.data,
      old_payload.size,
      a0_yyjson_read_flags(),
      &alc,
      &read_err);
  if (read_err.code) {
    return A0_MAKE_MSGERR("Failed to parse cfg: %s", read_err.msg);
  }

  yyjson_mut_doc* merged_doc = yyjson_mut_doc_new(NULL);

  merged_doc->root = yyjson_merge_patch(
      merged_doc,
      original->root,
      mergepatch->root);

  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_mut_write_opts(
      merged_doc,
      a0_yyjson_write_flags(),
      NULL,  // TODO(lshamis): Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    yyjson_mut_doc_free(merged_doc);
    return A0_MAKE_MSGERR("Failed to serialize cfg: %s", write_err.msg);
  }

  pkt->payload = (a0_buf_t){(uint8_t*)data, size};
  a0_err_t err = a0_middleware_chain(chain, pkt);

  free(data);
  yyjson_mut_doc_free(merged_doc);

  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mergepatch_process_locked(
    void* user_data,
    a0_transport_writer_locked_t* twl,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  yyjson_doc* mergepatch = (yyjson_doc*)user_data;

  bool empty;
  a0_transport_writer_empty(twl, &empty);

  if (empty) {
    return a0_mergepatch_process_locked_empty(mergepatch, twl, pkt, chain);
  }
  return a0_mergepatch_process_locked_nonempty(mergepatch, twl, pkt, chain);
}

a0_err_t a0_cfg_mergepatch_yyjson(a0_cfg_t* cfg, yyjson_doc mergepatch) {
  a0_middleware_t mergepatch_middleware = {
      .user_data = &mergepatch,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_mergepatch_process_locked,
  };

  a0_writer_t mergepatch_writer;
  a0_writer_wrap(
      &cfg->_writer,
      mergepatch_middleware,
      &mergepatch_writer);

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  a0_err_t err = a0_writer_write(&mergepatch_writer, pkt);

  a0_writer_close(&mergepatch_writer);
  return err;
}

#endif  // A0_EXT_YYJSON

a0_err_t a0_cfg_watcher_init(a0_cfg_watcher_t* cw,
                             a0_cfg_topic_t topic,
                             a0_alloc_t alloc,
                             a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_cfg_topic_open(topic, &cw->_file));

  a0_err_t err = a0_reader_init(
      &cw->_reader,
      cw->_file.arena,
      alloc,
      A0_INIT_MOST_RECENT,
      A0_ITER_NEWEST,
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
