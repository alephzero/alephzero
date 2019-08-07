#ifndef A0_ALEPHZERO_GO_H
#define A0_ALEPHZERO_GO_H

#include <a0/packet.h>
#include <a0/pubsub.h>

extern void a0go_alloc(void*, size_t, a0_buf_t*);
extern void a0go_callback(void*);
extern void a0go_subscriber_callback(void*, a0_packet_t);

static inline errno_t a0go_packet_build(a0_packet_builder_t builder,
                                        a0_packet_t* out,
                                        void* user_data) {
  a0_alloc_t alloc = {
      .user_data = user_data,
      .fn = a0go_alloc,
  };
  return a0_packet_build(builder, alloc, user_data);
}

static inline errno_t a0go_subscriber_sync_next(a0_subscriber_sync_t* sub_sync,
                                                a0_packet_t* pkt,
                                                void* user_data) {
  a0_alloc_t alloc = {
      .user_data = user_data,
      .fn = a0go_alloc,
  };
  return a0_subscriber_sync_next(sub_sync, alloc, pkt);
}

static inline errno_t a0go_subscriber_init_unmapped(a0_subscriber_t* sub,
                                                    const char* container,
                                                    const char* topic,
                                                    a0_subscriber_read_start_t read_start,
                                                    a0_subscriber_read_next_t read_next,
                                                    void* user_data) {
  a0_alloc_t alloc = {
      .user_data = user_data,
      .fn = a0go_alloc,
  };
  a0_subscriber_callback_t callback = {
      .user_data = user_data,
      .fn = a0go_subscriber_callback,
  };
  return a0_subscriber_init_unmapped(sub, container, topic, read_start, read_next, alloc, callback);
}

static inline errno_t a0go_subscriber_close(a0_subscriber_t* sub, void* user_data) {
  a0_callback_t callback = {
      .user_data = user_data,
      .fn = a0go_callback,
  };
  return a0_subscriber_close(sub, callback);
}

#endif A0_ALEPHZERO_GO_H
