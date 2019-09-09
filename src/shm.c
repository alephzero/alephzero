#include <a0/shm.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "macros.h"

errno_t a0_shm_open(const char* path, const a0_shm_options_t* opts, a0_shm_t* out) {
  out->fd = shm_open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
  A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(out->fd);
  A0_INTERNAL_CLEANUP_ON_MINUS_ONE(fstat(out->fd, &out->stat));

  if (opts) {
    if (opts->size != out->stat.st_size) {
      A0_INTERNAL_CLEANUP_ON_MINUS_ONE(ftruncate(out->fd, opts->size));
      A0_INTERNAL_CLEANUP_ON_MINUS_ONE(fstat(out->fd, &out->stat));
    }
  }

  out->ptr = (uint8_t*)mmap(
      /* addr   = */ 0,
      /* length = */ out->stat.st_size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ MAP_SHARED,
      /* fd     = */ out->fd,
      /* offset = */ 0);
  A0_INTERNAL_CLEANUP_ON_MINUS_ONE((intptr_t)out->ptr);

  return A0_OK;

cleanup:;
  errno_t err = errno;
  if (out->fd > 0) {
    close(out->fd);
  }
  return err;
}

errno_t a0_shm_unlink(const char* path) {
  A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(shm_unlink(path));
  return A0_OK;
}

errno_t a0_shm_close(a0_shm_t* shm) {
  if (shm->ptr) {
    A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(munmap(shm->ptr, shm->stat.st_size));
  }
  if (shm->fd) {
    A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(close(shm->fd));
  }
  return A0_OK;
}
