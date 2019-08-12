#ifndef A0_INTERNAL_RAND_H
#define A0_INTERNAL_RAND_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

_Thread_local unsigned short a0_xsubi[3];
_Thread_local bool a0_xsubi_init = false;

inline long int a0_mrand48() {
  if (A0_UNLIKELY(!a0_xsubi_init)) {
    // TODO: error handling.
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t todo_use_this = read(fd, a0_xsubi, sizeof(a0_xsubi));
    (void)todo_use_this;
    close(fd);
    a0_xsubi_init = true;
  }
  return jrand48(a0_xsubi);
}

#endif  // A0_INTERNAL_RAND_H
