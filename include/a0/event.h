#ifndef A0_EVENT_H
#define A0_EVENT_H

#include <a0/time.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_event_s {
  pthread_mutex_t _mu;
  pthread_cond_t _cv;
  bool _is_set;
} a0_event_t;

void a0_event_init(a0_event_t*);

void a0_event_close(a0_event_t*);

bool a0_event_is_set(a0_event_t*);

void a0_event_set(a0_event_t*);

void a0_event_wait(a0_event_t*);

void a0_event_timedwait(a0_event_t*, a0_time_mono_t timeout);

#ifdef __cplusplus
}
#endif

#endif  // A0_EVENT_H
