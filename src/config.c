#include <a0/alloc.h>
#include <a0/config.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/reader.h>
#include <a0/writer.h>

#include <stdlib.h>

#include "err_macro.h"
#include "topic.h"

#ifdef A0_C_CONFIG_USE_YYJSON

#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/middleware.h>
#include <a0/transport.h>
#include <a0/unused.h>

#include <alloca.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <yyjson.h>

#endif  // A0_C_CONFIG_USE_YYJSON

A0_STATIC_INLINE
a0_err_t a0_config_topic_open(a0_config_topic_t topic, a0_file_t* out) {
  return a0_topic_open(a0_env_topic_tmpl_cfg(), topic.name, topic.file_opts, out);
}

a0_err_t a0_config_init(a0_config_t* config, a0_config_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_config_topic_open(topic, &config->_file));
  a0_err_t err = a0_writer_init(&config->_writer, config->_file.arena);
  if (err) {
    a0_file_close(&config->_file);
  }
  return err;
}

a0_err_t a0_config_close(a0_config_t* config) {
  a0_writer_close(&config->_writer);
  a0_file_close(&config->_file);
  return A0_OK;
}

a0_err_t a0_config_read(a0_config_t* config,
                        a0_alloc_t alloc,
                        int flags,
                        a0_packet_t* out) {
  return a0_reader_read_one(config->_file.arena,
                            alloc,
                            A0_INIT_MOST_RECENT,
                            flags,
                            out);
}

a0_err_t a0_config_write(a0_config_t* config, a0_packet_t pkt) {
  return a0_writer_write(&config->_writer, pkt);
}

#ifdef A0_C_CONFIG_USE_YYJSON

static const int a0_yyjson_read_flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_INF_AND_NAN;

static const int a0_yyjson_write_flags = YYJSON_WRITE_ESCAPE_UNICODE | YYJSON_WRITE_ESCAPE_SLASHES;

typedef struct a0_yyjson_alloc_s {
  a0_alloc_t alloc;
  a0_buf_t pkt_buf;
  a0_buf_t yyjson_buf;
} a0_yyjson_alloc_t;

A0_STATIC_INLINE
a0_err_t yyjson_alloc_wrapper(void* user_data, size_t size, a0_buf_t* out) {
  a0_yyjson_alloc_t* yyjson_alloc = (a0_yyjson_alloc_t*)user_data;

  size_t yyjson_size = yyjson_read_max_memory_usage(
      size, a0_yyjson_read_flags);

  a0_buf_t buf;
  a0_err_t err = a0_alloc(yyjson_alloc->alloc, size + yyjson_size, &buf);

  *out = (a0_buf_t){buf.data, size};
  yyjson_alloc->pkt_buf = *out;
  yyjson_alloc->yyjson_buf = (a0_buf_t){buf.data + size, yyjson_size};
  return err;
}

a0_err_t a0_config_read_yyjson(a0_config_t* config,
                               a0_alloc_t alloc,
                               int flags,
                               yyjson_doc* out) {
  a0_yyjson_alloc_t yyjson_alloc = {alloc, A0_EMPTY, A0_EMPTY};

  a0_alloc_t alloc_wrapper = {
      .user_data = &yyjson_alloc,
      .alloc = yyjson_alloc_wrapper,
      .dealloc = alloc.dealloc,
  };

  a0_packet_t pkt;
  A0_RETURN_ERR_ON_ERR(a0_reader_read_one(
      config->_file.arena,
      alloc_wrapper,
      A0_INIT_MOST_RECENT,
      flags,
      &pkt));

  yyjson_alc alc;
  yyjson_alc_pool_init(
      &alc,
      yyjson_alloc.yyjson_buf.data,
      yyjson_alloc.yyjson_buf.size);

  yyjson_read_err read_err;
  yyjson_doc* result = yyjson_read_opts(
      (char*)pkt.payload.data,
      pkt.payload.size,
      a0_yyjson_read_flags,
      &alc,
      &read_err);
  if (read_err.code) {
    return A0_MAKE_MSGERR("Failed to parse config: %s", read_err.msg);
  }
  *out = *result;
  return A0_OK;
}

a0_err_t a0_config_write_yyjson(a0_config_t* config, yyjson_doc doc) {
  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_write_opts(
      &doc,
      a0_yyjson_write_flags,
      NULL,  // TODO: Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    return A0_MAKE_MSGERR("Failed to serialize config: %s", write_err.msg);
  }

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  pkt.payload = (a0_buf_t){(uint8_t*)data, size};

  a0_err_t err = a0_config_write(config, pkt);
  free(data);
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mergepatch_process_locked_empty(
    yyjson_doc* mergepatch,
    a0_transport_locked_t tlk,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(tlk);

  yyjson_write_err write_err;
  size_t size;
  char* data = yyjson_write_opts(
      mergepatch,
      a0_yyjson_write_flags,
      NULL,  // TODO: Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    free(data);
    return A0_MAKE_MSGERR("Failed to serialize config: %s", write_err.msg);
  }

  pkt->payload = (a0_buf_t){(uint8_t*)data, size};

  a0_err_t err = a0_middleware_chain(chain, pkt);
  free(data);
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mergepatch_process_locked_nonempty(
    yyjson_doc* mergepatch,
    a0_transport_locked_t tlk,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  a0_transport_jump_tail(tlk);
  a0_transport_frame_t frame;
  a0_transport_frame(tlk, &frame);

  a0_flat_packet_t flat_packet = {
      .buf = {frame.data, frame.hdr.data_size},
  };
  a0_buf_t old_payload;
  a0_flat_packet_payload(flat_packet, &old_payload);

  size_t yyjson_size = yyjson_read_max_memory_usage(
      old_payload.size, a0_yyjson_read_flags);

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
      a0_yyjson_read_flags,
      &alc,
      &read_err);
  if (read_err.code) {
    return A0_MAKE_MSGERR("Failed to parse config: %s", read_err.msg);
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
      a0_yyjson_write_flags,
      NULL,  // TODO: Maybe provide an allocator?
      &size,
      &write_err);

  if (write_err.code) {
    yyjson_mut_doc_free(merged_doc);
    return A0_MAKE_MSGERR("Failed to serialize config: %s", write_err.msg);
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
    a0_transport_locked_t tlk,
    a0_packet_t* pkt,
    a0_middleware_chain_t chain) {
  yyjson_doc* mergepatch = (yyjson_doc*)user_data;

  bool empty;
  a0_transport_empty(tlk, &empty);

  if (empty) {
    return a0_mergepatch_process_locked_empty(mergepatch, tlk, pkt, chain);
  }
  return a0_mergepatch_process_locked_nonempty(mergepatch, tlk, pkt, chain);
}

a0_err_t a0_config_mergepatch_yyjson(a0_config_t* config, yyjson_doc mergepatch) {
  a0_middleware_t mergepatch_middleware = {
      .user_data = &mergepatch,
      .close = NULL,
      .process = NULL,
      .process_locked = a0_mergepatch_process_locked,
  };

  a0_writer_t mergepatch_writer;
  a0_writer_wrap(
      &config->_writer,
      mergepatch_middleware,
      &mergepatch_writer);

  a0_packet_t pkt;
  a0_packet_init(&pkt);
  a0_err_t err = a0_writer_write(&mergepatch_writer, pkt);

  a0_writer_close(&mergepatch_writer);
  return err;
}

#endif  // A0_C_CONFIG_USE_YYJSON

a0_err_t a0_onconfig_init(a0_onconfig_t* cfg,
                          a0_config_topic_t topic,
                          a0_alloc_t alloc,
                          a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_config_topic_open(topic, &cfg->_file));

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
