#include <a0/inline.h>
#include <a0/time.h>

#include <pthread.h>
#include <stdbool.h>

#include "clock.h"

typedef struct a0_event_s {
  pthread_mutex_t _mu;
  pthread_cond_t _cv;
  bool _is_set;
} a0_event_t;

void a0_event_init(a0_event_t* evt) {
  pthread_mutex_init(&evt->_mu, NULL);

  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(&evt->_cv, &attr);

  evt->_is_set = false;
}

void a0_event_close(a0_event_t* evt) {
  pthread_mutex_destroy(&evt->_mu);
  pthread_cond_destroy(&evt->_cv);
}

bool a0_event_is_set(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  bool value = evt->_is_set;
  pthread_mutex_unlock(&evt->_mu);
  return value;
}

void a0_event_set(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  evt->_is_set = true;
  pthread_cond_broadcast(&evt->_cv);
  pthread_mutex_unlock(&evt->_mu);
}

void a0_event_wait(a0_event_t* evt) {
  pthread_mutex_lock(&evt->_mu);
  while (!evt->_is_set) {
    pthread_cond_wait(&evt->_cv, &evt->_mu);
  }
  pthread_mutex_unlock(&evt->_mu);
}

void a0_event_timedwait(a0_event_t* evt, a0_time_mono_t timeout) {
  timespec_t ts_mono;
  a0_clock_convert(CLOCK_BOOTTIME, timeout.ts, CLOCK_MONOTONIC, &ts_mono);

  pthread_mutex_lock(&evt->_mu);
  while (!evt->_is_set && pthread_cond_timedwait(&evt->_cv, &evt->_mu, &ts_mono) != ETIMEDOUT) {
  }
  pthread_mutex_unlock(&evt->_mu);
}
