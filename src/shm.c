#include <a0/common.h>
#include <a0/shm.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "macros.h"

const a0_shm_options_t A0_SHM_OPTIONS_DEFAULT = {
    .size = 16 * 1024 * 1024,
    .resize = false,
};

errno_t a0_shm_open(const char* path, const a0_shm_options_t* opts_, a0_shm_t* out) {
  const a0_shm_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_SHM_OPTIONS_DEFAULT;
  }

  int fd = shm_open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  A0_RETURN_ERR_ON_MINUS_ONE(fd);

  stat_t stat;
  A0_CLEANUP_ON_MINUS_ONE(fstat(fd, &stat));

  out->buf.size = stat.st_size;
  if ((opts->resize || !stat.st_size) && opts->size != stat.st_size) {
    A0_CLEANUP_ON_MINUS_ONE(ftruncate(fd, opts->size));
    out->buf.size = opts->size;
  }

  out->buf.ptr = (uint8_t*)mmap(
      /* addr   = */ 0,
      /* length = */ out->buf.size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ MAP_SHARED,
      /* fd     = */ fd,
      /* offset = */ 0);
  A0_CLEANUP_ON_MINUS_ONE((intptr_t)out->buf.ptr);

  close(fd);

  out->path = strdup(path);

  return A0_OK;

cleanup:;
  errno_t err = errno;
  if (fd > 0) {
    close(fd);
  }
  return err;
}

errno_t a0_shm_unlink(const char* path) {
  A0_RETURN_ERR_ON_MINUS_ONE(shm_unlink(path));
  return A0_OK;
}

errno_t a0_shm_close(a0_shm_t* shm) {
  if (!shm->buf.ptr && !shm->path) {
    return EBADF;
  }

  if (shm->buf.ptr) {
    A0_RETURN_ERR_ON_MINUS_ONE(munmap(shm->buf.ptr, shm->buf.size));
    shm->buf.ptr = NULL;
  }
  if (shm->path) {
    free((void*)shm->path);
    shm->path = NULL;
  }
  return A0_OK;
}
