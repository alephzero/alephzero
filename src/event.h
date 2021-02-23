#ifndef A0_SRC_EVENT_H
#define A0_SRC_EVENT_H

#include <a0/inline.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_event_s {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  bool is_set;
} a0_event_t;

A0_STATIC_INLINE
void a0_event_init(a0_event_t* evt) {
  pthread_mutex_init(&evt->mu, NULL);
  pthread_cond_init(&evt->cv, NULL);
  evt->is_set = false;
}

A0_STATIC_INLINE
void a0_event_close(a0_event_t* evt) {
  pthread_mutex_destroy(&evt->mu);
  pthread_cond_destroy(&evt->cv);
}

A0_STATIC_INLINE
void a0_event_set(a0_event_t* evt) {
  pthread_mutex_lock(&evt->mu);
  evt->is_set = true;
  pthread_cond_broadcast(&evt->cv);
  pthread_mutex_unlock(&evt->mu);
}

A0_STATIC_INLINE
void a0_event_wait(a0_event_t* evt) {
  pthread_mutex_lock(&evt->mu);
  while (!evt->is_set) {
    pthread_cond_wait(&evt->cv, &evt->mu);
  }
  pthread_mutex_unlock(&evt->mu);
}

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_EVENT_H
