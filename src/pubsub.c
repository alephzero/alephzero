#include <a0/alloc.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/reader.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/writer.h>

#include <stdbool.h>

#include "err_macro.h"

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

a0_err_t a0_publisher_writer(a0_publisher_t* pub, a0_writer_t** out) {
  *out = &pub->_writer;
  return A0_OK;
}

//////////////////
//  Subscriber  //
//////////////////

// Synchronous zero-copy version.

a0_err_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
                                    a0_pubsub_topic_t topic,
                                    a0_reader_options_t opts) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub_sync_zc->_file));

  a0_err_t err = a0_reader_sync_zc_init(
      &sub_sync_zc->_reader_sync_zc,
      sub_sync_zc->_file.arena,
      opts);
  if (err) {
    a0_file_close(&sub_sync_zc->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t* sub_sync_zc) {
  a0_reader_sync_zc_close(&sub_sync_zc->_reader_sync_zc);
  a0_file_close(&sub_sync_zc->_file);
  return A0_OK;
}

a0_err_t a0_subscriber_sync_zc_can_read(a0_subscriber_sync_zc_t* sub_sync_zc, bool* can_read) {
  return a0_reader_sync_zc_can_read(&sub_sync_zc->_reader_sync_zc, can_read);
}

a0_err_t a0_subscriber_sync_zc_read(a0_subscriber_sync_zc_t* sub_sync_zc, a0_zero_copy_callback_t onpacket) {
  return a0_reader_sync_zc_read(&sub_sync_zc->_reader_sync_zc, onpacket);
}

a0_err_t a0_subscriber_sync_zc_read_blocking(a0_subscriber_sync_zc_t* sub_sync_zc, a0_zero_copy_callback_t onpacket) {
  return a0_reader_sync_zc_read_blocking(&sub_sync_zc->_reader_sync_zc, onpacket);
}

a0_err_t a0_subscriber_sync_zc_read_blocking_timeout(a0_subscriber_sync_zc_t* sub_sync_zc, a0_time_mono_t* timeout, a0_zero_copy_callback_t onpacket) {
  return a0_reader_sync_zc_read_blocking_timeout(&sub_sync_zc->_reader_sync_zc, timeout, onpacket);
}

// Synchronous allocated version.

a0_err_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                 a0_pubsub_topic_t topic,
                                 a0_alloc_t alloc,
                                 a0_reader_options_t opts) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub_sync->_file));

  a0_err_t err = a0_reader_sync_init(
      &sub_sync->_reader_sync,
      sub_sync->_file.arena,
      alloc,
      opts);
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

a0_err_t a0_subscriber_sync_can_read(a0_subscriber_sync_t* sub_sync, bool* can_read) {
  return a0_reader_sync_can_read(&sub_sync->_reader_sync, can_read);
}

a0_err_t a0_subscriber_sync_read(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
  return a0_reader_sync_read(&sub_sync->_reader_sync, pkt);
}

a0_err_t a0_subscriber_sync_read_blocking(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
  return a0_reader_sync_read_blocking(&sub_sync->_reader_sync, pkt);
}

a0_err_t a0_subscriber_sync_read_blocking_timeout(a0_subscriber_sync_t* sub_sync, a0_time_mono_t* timeout, a0_packet_t* pkt) {
  return a0_reader_sync_read_blocking_timeout(&sub_sync->_reader_sync, timeout, pkt);
}

// Threaded zero-copy version.

a0_err_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
                               a0_pubsub_topic_t topic,
                               a0_reader_options_t opts,
                               a0_zero_copy_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub_zc->_file));

  a0_err_t err = a0_reader_zc_init(
      &sub_zc->_reader_zc,
      sub_zc->_file.arena,
      opts,
      onpacket);
  if (err) {
    a0_file_close(&sub_zc->_file);
    return err;
  }

  return A0_OK;
}

a0_err_t a0_subscriber_zc_close(a0_subscriber_zc_t* sub_zc) {
  a0_reader_zc_close(&sub_zc->_reader_zc);
  a0_file_close(&sub_zc->_file);
  return A0_OK;
}

// Threaded allocated version.

a0_err_t a0_subscriber_init(a0_subscriber_t* sub,
                            a0_pubsub_topic_t topic,
                            a0_alloc_t alloc,
                            a0_reader_options_t opts,
                            a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_pubsub_topic_open(topic, &sub->_file));

  a0_err_t err = a0_reader_init(
      &sub->_reader,
      sub->_file.arena,
      alloc,
      opts,
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
