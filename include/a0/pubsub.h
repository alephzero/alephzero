#ifndef A0_PUBSUB_H
#define A0_PUBSUB_H

#include <a0/packet.h>
#include <a0/stream.h>

typedef struct a0_topic_s a0_topic_t;

typedef struct a0_publisher_s a0_publisher_t;
typedef struct a0_subscriber_s a0_subscriber_t;
void (*a0_subscriber_callback_t)(a0_subscriber_t*, a0_packet_t*);

struct a0_topic_s {
  a0_buf_t name;
  a0_buf_t as_container;
};

struct a0_publisher_s {
  // Required from user.
  a0_topic_t topic;

  // Optional from user.
  a0_shmobj_options_t shmobj_options;

  // Private.
  a0_shmobj_t _shmobj;
  a0_stream_options_t _stream_options;
  a0_stream_construct_options_t _stream_construct_options;
  a0_stream_t _stream;
};

errno_t a0_publisher_init(a0_publisher_t*);
errno_t a0_publisher_close(a0_publisher_t*);
errno_t a0_pub(a0_publisher_t*, a0_packet_t*);


// struct a0_subscriber_s {
//   void* user_data;

//   a0_loop_t loop;
//   a0_topic_t topic;
//   a0_subscriber_read_start_t read_start;
//   a0_subscriber_read_next_t read_next;

//   a0_subscriber_callback_t callback;
// };

// errno_t a0_subscriber_init(a0_subscriber_t*);
// errno_t a0_subscriber_close(a0_subscriber_t*);

#endif  // A0_PUBSUB_H
