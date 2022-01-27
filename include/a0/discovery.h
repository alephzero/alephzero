#ifndef A0_DISCOVERY_H
#define A0_DISCOVERY_H

#include <a0/err.h>
#include <a0/map.h>
#include <a0/pathglob.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_discovery_callback_s {
  void* user_data;
  void (*fn)(void* user_data, const char* path);
} a0_discovery_callback_t;

typedef struct a0_discovery_s {
  a0_pathglob_t _pathglob;
  a0_discovery_callback_t _callback;
  pthread_t _thread;

  a0_map_t _watch_map;          // inotify watch descriptor -> absolute dir path
  a0_map_t _reverse_watch_map;  // absolute dir path -> nothing
  a0_map_t _discovered_map;     // absolute file path -> nothing
  int _epoll_fd;
  int _inotify_fd;
  int _close_fd;
} a0_discovery_t;

a0_err_t a0_discovery_init(a0_discovery_t*, const char* path_pattern, a0_discovery_callback_t callback);
a0_err_t a0_discovery_close(a0_discovery_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_DISCOVERY_H
