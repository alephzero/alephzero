#ifndef A0_EVENT_H
#define A0_EVENT_H

#include <a0/inline.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_event_s {
  pthread_mutex_t _mu;
  pthread_cond_t _cv;
  bool _is_set;
} a0_event_t;

A0_STATIC_INLINE
void a0_event_init(a0_event_t* evt) {
  pthread_mutex_init(&evt->_mu, NULL);
  pthread_cond_init(&evt->_cv, NULL);
  evt->_is_set = false;
}

A0_STATIC_INLINE
void a0_event_close(a0_event_t* evt) {
  pthread_mutex_destroy(&evt->_mu);
  pthread_cond_destroy(&evt->_cv);
}

A0_STATIC_INLINE
bool a0_event_is_set(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  bool value = evt->_is_set;
  pthread_mutex_unlock(&evt->_mu);
  return value;
}

A0_STATIC_INLINE
void a0_event_set(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  evt->_is_set = true;
  pthread_cond_broadcast(&evt->_cv);
  pthread_mutex_unlock(&evt->_mu);
}

A0_STATIC_INLINE
void a0_event_wait(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  while (!evt->_is_set) {
    pthread_cond_wait(&evt->_cv, &evt->_mu);
  }
  pthread_mutex_unlock(&evt->_mu);
}

#ifdef __cplusplus
}
#endif

#endif  // A0_EVENT_H
