#include "rand.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

thread_local uint16_t _a0_xsubi[3];
thread_local bool _a0_xsubi_init = false;

uint32_t a0_mrand48() {
  if (!_a0_xsubi_init) {
    // TODO(lshamis): error handling.
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t todo_use_this = read(fd, _a0_xsubi, sizeof(_a0_xsubi));
    (void)todo_use_this;
    close(fd);
    _a0_xsubi_init = true;
  }
  return jrand48(_a0_xsubi);
}
