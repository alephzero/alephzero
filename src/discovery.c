#include <a0/buf.h>
#include <a0/cmp.h>
#include <a0/discovery.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/map.h>
#include <a0/pathglob.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "err_macro.h"

typedef struct epoll_event epoll_event_t;
typedef struct inotify_event inotify_event_t;

A0_STATIC_INLINE
size_t a0_discovery_rootlen(a0_discovery_t* d) {
  a0_pathglob_t* glob = &d->_pathglob;
  char* start_ptr = (char*)glob->parts[0].str.data - 1;
  for (size_t i = 0; i + 1 < glob->depth; i++) {
    char* exclude_ptr = (char*)glob->parts[i].str.data;

    if (glob->parts[i].type != A0_PATHGLOB_PART_TYPE_VERBATIM) {
      return exclude_ptr - start_ptr;
    }

    char* end_ptr = exclude_ptr + glob->parts[i].str.size + 1;
    char old = *end_ptr;
    *end_ptr = '\0';

    stat_t unused;
    int err = stat(start_ptr, &unused);

    *end_ptr = old;
    if (err) {
      return exclude_ptr - start_ptr;
    }
  }
  return (char*)glob->parts[glob->depth - 1].str.data - start_ptr;
}

A0_STATIC_INLINE
void a0_discovery_watch_path(a0_discovery_t* d, const char* path) {
  bool already_watching;
  a0_map_has(&d->_reverse_watch_map, &path, &already_watching);
  if (already_watching) {
    return;
  }

  int wd = inotify_add_watch(d->_inotify_fd, path, IN_CREATE);
  if (wd < 0) {
    return;
  }
  char* dup = strdup(path);
  a0_map_put(&d->_watch_map, &wd, &dup);
  int unused = 0;
  a0_map_put(&d->_reverse_watch_map, &dup, &unused);
}

A0_STATIC_INLINE
void a0_discovery_announce(a0_discovery_t* d, const char* path) {
  bool contains;
  a0_map_has(&d->_discovered_map, &path, &contains);
  if (!contains) {
    char* dup = strdup(path);
    int unused = 0;
    a0_map_put(&d->_discovered_map, &dup, &unused);
    d->_callback.fn(d->_callback.user_data, path);
  }
}

A0_STATIC_INLINE_RECURSIVE
void a0_discovery_watch_recursive(a0_discovery_t* d, const char* path) {
  a0_discovery_watch_path(d, path);

  a0_file_iter_t iter;
  a0_file_iter_entry_t entry;
  a0_file_iter_init(&iter, path);
  while (!a0_file_iter_next(&iter, &entry)) {
    if (entry.d_type == DT_DIR) {
      a0_discovery_watch_recursive(d, entry.fullpath);
    } else if (entry.d_type == DT_REG) {
      bool match = false;
      a0_pathglob_match(&d->_pathglob, entry.fullpath, &match);
      if (match) {
        a0_discovery_announce(d, entry.fullpath);
      }
    }
  }
  a0_file_iter_close(&iter);
}

A0_STATIC_INLINE
void a0_discovery_watch_root(a0_discovery_t* d) {
  size_t root_size = a0_discovery_rootlen(d);
  char root_path[PATH_MAX];
  memcpy(root_path, d->_pathglob.abspath.data, root_size);
  root_path[root_size] = '\0';
  a0_discovery_watch_recursive(d, root_path);
}

A0_STATIC_INLINE
void a0_discovery_closefd_init(a0_discovery_t* d, int epoll_fd) {
  d->_close_fd = eventfd(0, EFD_NONBLOCK);

  epoll_event_t evt = A0_EMPTY;
  evt.events = EPOLLIN;
  evt.data.fd = d->_close_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, d->_close_fd, &evt);
}

A0_STATIC_INLINE
void a0_discovery_inotify_init(a0_discovery_t* d, int epoll_fd) {
  d->_inotify_fd = inotify_init1(IN_NONBLOCK);

  epoll_event_t evt = A0_EMPTY;
  evt.events = EPOLLIN;
  evt.data.fd = d->_inotify_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, d->_inotify_fd, &evt);
}

A0_STATIC_INLINE
a0_err_t a0_discovery_inotify_runone(a0_discovery_t* d) {
  uint8_t buf[sizeof(inotify_event_t) + PATH_MAX];
  int buf_size = read(d->_inotify_fd, buf, sizeof(buf));

  if (!buf_size) {
    return A0_OK;
  }

  if (buf_size == -1) {
    if (errno == EAGAIN) {
      return A0_OK;
    }
    return A0_MAKE_SYSERR(errno);
  }

  for (size_t i = 0; i < (size_t)buf_size;) {
    inotify_event_t* evt = (inotify_event_t*)&buf[i];
    if (evt->len) {
      a0_buf_t path = (a0_buf_t){(uint8_t*)evt->name, strlen(evt->name)};

      bool is_dir = evt->mask & IN_ISDIR;

      const char** parent;
      a0_map_get(&d->_watch_map, &evt->wd, (void**)&parent);

      size_t parent_size = strlen(*parent);
      bool parent_ends_with_slash = (*parent)[parent_size - 1] == '/';

      char* abspath = (char*)malloc(parent_size + !parent_ends_with_slash + path.size + 1);
      memcpy(abspath, *parent, parent_size);
      if (!parent_ends_with_slash) {
        abspath[parent_size] = '/';
      }
      memcpy(abspath + parent_size + !parent_ends_with_slash, path.data, path.size);
      abspath[parent_size + !parent_ends_with_slash + path.size] = '\0';

      if (is_dir) {
        a0_discovery_watch_recursive(d, abspath);
      } else {
        bool is_match = false;
        a0_pathglob_match(&d->_pathglob, abspath, &is_match);
        if (is_match) {
          a0_discovery_announce(d, abspath);
        }
      }

      free(abspath);
    }
    i += sizeof(inotify_event_t) + evt->len;
  }

  return A0_OK;
}

A0_STATIC_INLINE
void a0_discovery_epoll_init(a0_discovery_t* d) {
  d->_epoll_fd = epoll_create1(0);
}

A0_STATIC_INLINE
a0_err_t a0_discovery_epoll_runone(a0_discovery_t* d) {
  epoll_event_t evt = A0_EMPTY;
  int num_evt = epoll_wait(d->_epoll_fd, &evt, 1, -1);

  if (!num_evt) {
    return A0_OK;
  }

  if (num_evt == -1) {
    if (errno == EINTR) {
      return A0_OK;
    }
    return A0_MAKE_SYSERR(errno);
  }

  if (evt.data.fd == d->_inotify_fd) {
    A0_RETURN_ERR_ON_ERR(a0_discovery_inotify_runone(d));
  }
  if (evt.data.fd == d->_close_fd) {
    return A0_ERR_ITER_DONE;
  }
  return A0_OK;
}

A0_STATIC_INLINE
void* a0_discovery_thread(void* arg) {
  a0_discovery_t* d = (a0_discovery_t*)arg;

  a0_discovery_watch_root(d);

  while (true) {
    a0_err_t err = a0_discovery_epoll_runone(d);
    if (err) {
      // TODO(lshamis): Report err if not A0_ERR_ITER_DONE.
      break;
    }
  }
  return NULL;
}

a0_err_t a0_discovery_init(a0_discovery_t* d, const char* path_pattern, a0_discovery_callback_t callback) {
  d->_callback = callback;

  a0_err_t err = a0_pathglob_init(&d->_pathglob, path_pattern);
  if (err) {
    return err;
  }

  err = a0_map_init(
      &d->_watch_map,
      sizeof(int),
      sizeof(char*),
      A0_HASH_U32,
      A0_CMP_U32);
  if (err) {
    a0_pathglob_close(&d->_pathglob);
    return err;
  }

  err = a0_map_init(
      &d->_reverse_watch_map,
      sizeof(char*),
      sizeof(int),
      A0_HASH_STR,
      A0_CMP_STR);
  if (err) {
    a0_map_close(&d->_watch_map);
    a0_pathglob_close(&d->_pathglob);
    return err;
  }

  err = a0_map_init(
      &d->_discovered_map,
      sizeof(char*),
      sizeof(int),
      A0_HASH_STR,
      A0_CMP_STR);
  if (err) {
    a0_map_close(&d->_reverse_watch_map);
    a0_map_close(&d->_watch_map);
    a0_pathglob_close(&d->_pathglob);
    return err;
  }

  a0_discovery_epoll_init(d);
  a0_discovery_closefd_init(d, d->_epoll_fd);
  a0_discovery_inotify_init(d, d->_epoll_fd);

  pthread_create(&d->_thread, NULL, a0_discovery_thread, d);

  return A0_OK;
}

a0_err_t a0_discovery_close(a0_discovery_t* d) {
  uint64_t writ = 1;
  (void)!write(d->_close_fd, &writ, sizeof(uint64_t));
  pthread_join(d->_thread, NULL);

  a0_map_iterator_t iter;
  const void* key;
  void* val;

  a0_map_iterator_init(&iter, &d->_watch_map);
  while (!a0_map_iterator_next(&iter, &key, &val)) {
    inotify_rm_watch(d->_inotify_fd, *(int*)key);
    free(*(void**)val);
  }
  a0_map_close(&d->_watch_map);
  // Note: _reverse_watch_map doesn't own any memory.
  a0_map_close(&d->_reverse_watch_map);

  a0_map_iterator_init(&iter, &d->_discovered_map);
  while (!a0_map_iterator_next(&iter, &key, &val)) {
    free(*(void**)key);
  }
  a0_map_close(&d->_discovered_map);

  a0_pathglob_close(&d->_pathglob);
  close(d->_epoll_fd);
  close(d->_inotify_fd);
  close(d->_close_fd);

  return A0_OK;
}