///////////////
// deadman.h //
///////////////
#ifndef A0_DEADMAN_H
#define A0_DEADMAN_H

#include <a0/empty.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/mtx.h>
#include <a0/time.h>

#include <errno.h>
#include <stdint.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_deadman_topic_s {
  const char* name;
} a0_deadman_topic_t;

typedef struct a0_deadman_file_content_s {
  pid_t pid;
} a0_deadman_file_content_t;

typedef struct a0_deadman_s {
  a0_file_t _file;
  bool _is_owner;
  pid_t _owner_pid;

  pthread_t _thread;

  int _epoll_fd;
  int _inotify_fd;
  int _inotify_wd;
  int _pid_fd;
  int _event_fd;

  a0_mtx_t _mtx;
  a0_cnd_t _cnd;
} a0_deadman_t;

typedef struct a0_deadman_state_s {
  bool is_taken;
  bool is_owner;
  uint64_t tkn;
} a0_deadman_state_t;

a0_err_t a0_deadman_init(a0_deadman_t*, a0_deadman_topic_t);
a0_err_t a0_deadman_close(a0_deadman_t*);

a0_err_t a0_deadman_take(a0_deadman_t*);
a0_err_t a0_deadman_trytake(a0_deadman_t*);
a0_err_t a0_deadman_timedtake(a0_deadman_t*, a0_time_mono_t*);
a0_err_t a0_deadman_release(a0_deadman_t*);
a0_err_t a0_deadman_wait_taken(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedwait_taken(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_wait_released(a0_deadman_t*, uint64_t tkn);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t*, uint64_t tkn);
a0_err_t a0_deadman_state(a0_deadman_t*, a0_deadman_state_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_DEADMAN_H

///////////////
// deadman.c //
///////////////

A0_STATIC_INLINE
int pidfd_open(pid_t pid, int flags) {
  return syscall(SYS_pidfd_open, pid, flags);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_topic_open(a0_deadman_topic_t topic, a0_file_t* file) {
  a0_file_options_t opts = A0_FILE_OPTIONS_DEFAULT;
  opts.create_options.size = sizeof(a0_deadman_file_content_t);
  return a0_topic_open(a0_env_topic_tmpl_deadman(), topic.name, &opts, file);
}

A0_STATIC_INLINE
a0_err_t epoll_add_fd(int epoll_fd, int fd) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  A0_RETURN_SYSERR_ON_MINUS_ONE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t epoll_del_fd(int epoll_fd, int fd) {
  A0_RETURN_SYSERR_ON_MINUS_ONE(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL));
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_init_epoll(a0_deadman_t* d) {
  d->_epoll_fd = epoll_create1(0);
  A0_RETURN_SYSERR_ON_MINUS_ONE(d->_epoll_fd);
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_deadman_init_inotify(a0_deadman_t* d) {
  d->_inotify_fd = inotify_init1(IN_NONBLOCK);
  A0_RETURN_SYSERR_ON_MINUS_ONE(d->_inotify_fd);

  d->_inotify_wd = inotify_add_watch(d->_inotify_fd, d->_file.path, IN_CLOSE);
  A0_RETURN_SYSERR_ON_MINUS_ONE(d->_inotify_wd);

  return epoll_add_fd(d->_epoll_fd, d->_inotify_fd);
}

A0_STATIC_INLINE
a0_err_t a0_deadman_init_event(a0_deadman_t* d) {
  d->_event_fd = eventfd(0, EFD_NONBLOCK);
  A0_RETURN_SYSERR_ON_MINUS_ONE(d->_event_fd);

  return epoll_add_fd(d->_epoll_fd, d->_event_fd);
}

A0_STATIC_INLINE
void a0_deadman_undo_init(a0_deadman_t* d) {
  a0_file_close(&d->_file);

  if (d->_event_fd >= 0) {
    close(d->_event_fd);
  }
  if (d->_epoll_fd >= 0) {
    close(d->_epoll_fd);
  }
  if (d->_inotify_fd >= 0) {
    close(d->_inotify_fd);
  }
}

A0_STATIC_INLINE
void a0_deadman_pid_update(a0_deadman_t* d) {
  // Inotify event indicates that the pid file may have been edited.
  a0_deadman_file_content_t* content = (a0_deadman_file_content_t)d->_file.arena.buf.data;
  pid_t owner_pid = a0_atomic_load(content->pid);
  a0_mtx_lock(&d->_mtx);
  if (d->_owner_pid != owner_pid) {
    d->_owner_pid = owner_pid;

    if (d->_pid_fd) {
      close(d->_pid_fd);
      d->_pid_fd = 0;
    }
    if (d->_owner_pid) {
      // Release the old pid_fd.
      if (d->_pid_fd) {
        epoll_del_fd(d->_epoll_fd, d->_pid_fd);
        close(d->_pid_fd);
      }
      d->_pid_fd = pidfd_open(d->_owner_pid, PIDFD_NONBLOCK);
      if (d->_pid_fd >= 0) {
        epoll_add_fd(d->_epoll_fd, d->_pid_fd);
      } else {
        d->_pid_fd = 0;
        d->_owner_pid = 0;
      }
    }

    a0_cnd_signal(&d->_cnd, &d->_mtx);
  }
  a0_mtx_unlock(&d->_mtx);
}

A0_STATIC_INLINE
void a0_deadman_epoll_loop(a0_deadman_t* d) {
  struct epoll_event ev;
  while (true) {
    epoll_wait(d->_epoll_fd, &ev, 1, -1);
    if (ev.data.fd == d->_event_fd) {
      // Eventfd event indicates that the deadman object is closing.
      // Release the thread.
      break;
    } else if (ev.data.fd == d->_inotify_fd) {
      a0_deadman_pid_update(d);
    } else if (ev.data.fd == d->_pid_fd) {
      a0_deadman_file_content_t* content = (a0_deadman_file_content_t)d->_file.arena.buf.data;
      a0_cas(&content->pid, d->_owner_pid, 0);
      a0_deadman_pid_update(d);
    }
  }
}

a0_err_t a0_deadman_init(a0_deadman_t* d, a0_deadman_topic_t topic) {
  *d = (a0_deadman_t)A0_EMPTY;
  A0_RETURN_ERR_ON_ERR(a0_deadman_topic_open(topic, &d->_file));
  d->_is_owner = false;

  a0_err_t err = a0_deadman_init_epoll(d);
  if (err) {
    a0_deadman_undo_init(d);
    return err;
  }

  a0_deadman_pid_update(d);
  pthread_create(&d->_thread, NULL, a0_deadman_epoll_loop, d);

  return A0_OK;
}

a0_err_t a0_deadman_close(a0_deadman_t* d) {
  // Trigger the eventfd to wake up the epoll_wait.
  uint64_t val = 1;
  eventfd_write(d->_event_fd, val);

  // Wait for the thread to join.
  pthread_join(d->_thread, NULL);

  // Close the files.
  a0_deadman_undo_init(d);
}

a0_err_t a0_deadman_take(a0_deadman_t* d) {
  return a0_deadman_timedtake(d, NULL);
}

a0_err_t a0_deadman_trytake(a0_deadman_t* d) {
  a0_deadman_file_content_t* content = (a0_deadman_file_content_t)d->_file.arena.buf.data;
  pid_t pid = getpid();
  if (a0_atomic_load(&content->pid) == pid || a0_cas(&content->pid, 0, pid)) {
    return A0_OK;
  }
  return A0_MAKE_SYSERR(EBUSY);
}

a0_err_t a0_deadman_timedtake(a0_deadman_t* d, a0_time_mono_t* timeout) {
  a0_err_t err = A0_OK;
  a0_mtx_lock(&d->_mtx);
  while (a0_deadman_trytake(d)) {
    err = a0_cnd_timedwait(&d->_cnd, &d->_mtx, timeout);
    if (err) {
      break;
    }
  }
  a0_mtx_unlock(&d->_mtx);
  return err;
}

a0_err_t a0_deadman_release(a0_deadman_t*);
a0_err_t a0_deadman_wait_taken(a0_deadman_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_timedwait_taken(a0_deadman_t*, a0_time_mono_t*, uint64_t* out_tkn);
a0_err_t a0_deadman_wait_released(a0_deadman_t*, uint64_t tkn);
a0_err_t a0_deadman_timedwait_released(a0_deadman_t*, a0_time_mono_t*, uint64_t tkn);
a0_err_t a0_deadman_state(a0_deadman_t*, a0_deadman_state_t*);
