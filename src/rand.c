#include "rand.h"

#include <a0/thread_local.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

A0_THREAD_LOCAL uint16_t a0_xsubi[3];
A0_THREAD_LOCAL bool a0_xsubi_init = false;

uint32_t a0_mrand48() {
  if (!a0_xsubi_init) {
    // TODO(lshamis): error handling.
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t todo_use_this = read(fd, a0_xsubi, sizeof(a0_xsubi));
    (void)todo_use_this;
    close(fd);
    a0_xsubi_init = true;
  }
  return jrand48(a0_xsubi);
}
