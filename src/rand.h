#ifndef A0_SRC_RAND_H
#define A0_SRC_RAND_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

extern __thread unsigned short a0_xsubi[3];
extern __thread bool a0_xsubi_init;

A0_STATIC_INLINE
long int a0_mrand48() {
  if (A0_UNLIKELY(!a0_xsubi_init)) {
    // TODO(lshamis): error handling.
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t todo_use_this = read(fd, a0_xsubi, sizeof(a0_xsubi));
    (void)todo_use_this;
    close(fd);
    a0_xsubi_init = true;
  }
  return jrand48(a0_xsubi);
}

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_RAND_H
