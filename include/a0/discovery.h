#ifndef A0_DISCOVERY_H
#define A0_DISCOVERY_H

#include <a0/buf.h>
#include <a0/err.h>
#include <a0/map.h>

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { A0_PATHGLOB_MAX_DEPTH = 32 };

typedef enum a0_pathglob_part_type_e {
  A0_PATHGLOB_PART_TYPE_VERBATIM,
  A0_PATHGLOB_PART_TYPE_PATTERN,
  A0_PATHGLOB_PART_TYPE_RECURSIVE,
} a0_pathglob_part_type_t;

typedef struct a0_pathglob_part_s {
  a0_buf_t str;
  a0_pathglob_part_type_t type;
} a0_pathglob_part_t;

typedef struct a0_pathglob_s {
  a0_pathglob_part_t parts[A0_PATHGLOB_MAX_DEPTH];
  size_t depth;
} a0_pathglob_t;

a0_err_t a0_pathglob_init(a0_pathglob_t*, const char* path_pattern);

a0_err_t a0_pathglob_match(a0_pathglob_t*, const char* path, bool* out);

typedef struct a0_discovery_callback_s {
  void* user_data;
  void (*fn)(void* user_data, const char* path);
} a0_discovery_callback_t;

typedef struct a0_discovery_s {
  char* _path_pattern;
  a0_pathglob_t _pathglob;
  a0_discovery_callback_t _callback;
  pthread_t _thread;

  a0_map_t _watch_map;       // inotify watch descriptor -> absolute path
  a0_map_t _discovered_map;  // absolute path -> nothing
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
