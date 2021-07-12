#include <a0/alloc.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>
#include <a0/pubsub.h>
#include <a0/reader.h>
#include <a0/writer.h>
#include <a0/writer_middleware.h>

#include <stdbool.h>

#include "err_util.h"
#include "protocol_util.h"

/////////////////
//  Publisher  //
/////////////////

errno_t a0_publisher_init(a0_publisher_t* pub, a0_pubsub_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &pub->_file));

  errno_t err = a0_writer_init(&pub->_simple_writer, pub->_file.arena);
  if (err) {
    a0_file_close(&pub->_file);
    return err;
  }

  err = a0_writer_wrap(
      &pub->_simple_writer,
      a0_writer_middleware_add_standard_headers(),
      &pub->_annotated_writer);
  if (err) {
    a0_writer_close(&pub->_simple_writer);
    a0_file_close(&pub->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_publisher_close(a0_publisher_t* pub) {
  a0_writer_close(&pub->_annotated_writer);
  a0_writer_close(&pub->_simple_writer);
  a0_file_close(&pub->_file);
  return A0_OK;
}

errno_t a0_publisher_pub(a0_publisher_t* pub, a0_packet_t pkt) {
  return a0_writer_write(&pub->_annotated_writer, pkt);
}

//////////////////
//  Subscriber  //
//////////////////

// Synchronous zero-copy version.

errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_pubsub_topic_t topic,
                                   a0_reader_init_t init,
                                   a0_reader_iter_t iter) {
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &sub_sync_zc->_file));

  errno_t err = a0_reader_sync_zc_init(
      &sub_sync_zc->_reader_sync_zc,
      sub_sync_zc->_file.arena,
      init,
      iter);
  if (err) {
    a0_file_close(&sub_sync_zc->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t* sub_sync_zc) {
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_zc_close(&sub_sync_zc->_reader_sync_zc));
  a0_file_close(&sub_sync_zc->_file);
  return A0_OK;
}

errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t* sub_sync_zc, bool* has_next) {
  return a0_reader_sync_zc_has_next(&sub_sync_zc->_reader_sync_zc, has_next);
}

errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t* sub_sync_zc,
                                   a0_zero_copy_callback_t cb) {
  return a0_reader_sync_zc_next(&sub_sync_zc->_reader_sync_zc, cb);
}

// Synchronous allocated version.

errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
                                a0_pubsub_topic_t topic,
                                a0_alloc_t alloc,
                                a0_reader_init_t init,
                                a0_reader_iter_t iter) {
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &sub_sync->_file));

  errno_t err = a0_reader_sync_init(
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

errno_t a0_subscriber_sync_close(a0_subscriber_sync_t* sub_sync) {
  A0_RETURN_ERR_ON_ERR(a0_reader_sync_close(&sub_sync->_reader_sync));
  a0_file_close(&sub_sync->_file);
  return A0_OK;
}

errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t* sub_sync, bool* has_next) {
  return a0_reader_sync_has_next(&sub_sync->_reader_sync, has_next);
}

errno_t a0_subscriber_sync_next(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
  return a0_reader_sync_next(&sub_sync->_reader_sync, pkt);
}

// Zero-copy threaded version.

errno_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
                              a0_pubsub_topic_t topic,
                              a0_reader_init_t init,
                              a0_reader_iter_t iter,
                              a0_zero_copy_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &sub_zc->_file));

  errno_t err = a0_reader_zc_init(
      &sub_zc->_reader_zc,
      sub_zc->_file.arena,
      init,
      iter,
      onpacket);
  if (err) {
    a0_file_close(&sub_zc->_file);
    return err;
  }

  return A0_OK;
}

errno_t a0_subscriber_zc_close(a0_subscriber_zc_t* sub_zc) {
  A0_RETURN_ERR_ON_ERR(a0_reader_zc_close(&sub_zc->_reader_zc));
  a0_file_close(&sub_zc->_file);
  return A0_OK;
}

// Normal threaded version.

errno_t a0_subscriber_init(a0_subscriber_t* sub,
                           a0_pubsub_topic_t topic,
                           a0_alloc_t alloc,
                           a0_reader_init_t init,
                           a0_reader_iter_t iter,
                           a0_packet_callback_t onpacket) {
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &sub->_file));

  errno_t err = a0_reader_init(
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

errno_t a0_subscriber_close(a0_subscriber_t* sub) {
  A0_RETURN_ERR_ON_ERR(a0_reader_close(&sub->_reader));
  a0_file_close(&sub->_file);
  return A0_OK;
}

// One-off reader.

errno_t a0_subscriber_read_one(a0_pubsub_topic_t topic,
                               a0_alloc_t alloc,
                               a0_reader_init_t init,
                               int flags,
                               a0_packet_t* out) {
  a0_file_t file;
  A0_RETURN_ERR_ON_ERR(a0_open_topic("pubsub", topic.name, topic.file_opts, &file));

  errno_t err = a0_reader_read_one(file.arena,
                                   alloc,
                                   init,
                                   flags,
                                   out);

  a0_file_close(&file);
  return err;
}
