#include <a0/alloc.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/reader.h>
#include <a0/writer.h>

#include <stdbool.h>

#include "err_macro.h"
#include "topic.h"

A0_STATIC_INLINE
a0_err_t a0_pubsub_topic_open(a0_pubsub_topic_t topic, a0_file_t* file) {
  return a0_topic_open(a0_env_topic_tmpl_pubsub(), topic.name, topic.file_opts, file);
}

/////////////////
//  Publisher  //
/////////////////

a0_err_t a0_publisher_init(a0_publisher_t* pub, a0_pubsub_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &pub->_file));

  a0_err_t err = a0_writer_init(&pub->_writer, pub->_file.arena);
  if (err) {
    a0_file_close(&pub->_file);
    return err;
  }

  err = a0_writer_push(&pub->_writer, a0_add_standard_headers());
  if (err) {
    a0_writer_close(&pub->_writer);
    a0_file_close(&pub->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_publisher_close(a0_publisher_t* pub) {
  a0_writer_close(&pub->_writer);
  a0_file_close(&pub->_file);
  return A0_OK;
}

a0_err_t a0_publisher_pub(a0_publisher_t* pub, a0_packet_t pkt) {
  return a0_writer_write(&pub->_writer, pkt);
}

//////////////////
//  Subscriber  //
//////////////////

// Synchronous allocated version.

a0_err_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                 a0_pubsub_topic_t topic,
                                 a0_alloc_t alloc,
                                 a0_reader_init_t init,
                                 a0_reader_iter_t iter) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub_sync->_file));

  a0_err_t err = a0_reader_sync_init(
      &sub_sync->_reader_sync,
      sub_sync->_file.arena,
      alloc,
      init,
      iter);
  if (err) {
    a0_file_close(&sub_sync->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_subscriber_sync_close(a0_subscriber_sync_t* sub_sync) {
  a0_reader_sync_close(&sub_sync->_reader_sync);
  a0_file_close(&sub_sync->_file);
  return A0_OK;
}

a0_err_t a0_subscriber_sync_has_next(a0_subscriber_sync_t* sub_sync, bool* has_next) {
  return a0_reader_sync_has_next(&sub_sync->_reader_sync, has_next);
}

a0_err_t a0_subscriber_sync_next(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
  return a0_reader_sync_next(&sub_sync->_reader_sync, pkt);
}

// Normal threaded version.

a0_err_t a0_subscriber_init(a0_subscriber_t* sub,
                            a0_pubsub_topic_t topic,
                            a0_alloc_t alloc,
                            a0_reader_init_t init,
                            a0_reader_iter_t iter,
                            a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub->_file));

  a0_err_t err = a0_reader_init(
      &sub->_reader,
      sub->_file.arena,
      alloc,
      init,
      iter,
      onpacket);
  if (err) {
    a0_file_close(&sub->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_subscriber_close(a0_subscriber_t* sub) {
  a0_reader_close(&sub->_reader);
  a0_file_close(&sub->_file);
  return A0_OK;
}

// One-off reader.

a0_err_t a0_subscriber_read_one(a0_pubsub_topic_t topic,
                                a0_alloc_t alloc,
                                a0_reader_init_t init,
                                int flags,
                                a0_packet_t* out) {
  a0_file_t file;
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &file));

  a0_err_t err = a0_reader_read_one(file.arena,
                                    alloc,
                                    init,
                                    flags,
                                    out);

  a0_file_close(&file);
  return err;
}
