#include <a0/shmobj.h>

#include <a0/internal/macros.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

errno_t a0_shmobj_open(const char* path,
                       const a0_shmobj_options_t* opts,
                       a0_shmobj_t* out) {
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

errno_t a0_shmobj_unlink(const char* path) {
  A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(shm_unlink(path));
  return A0_OK;
}

errno_t a0_shmobj_close(a0_shmobj_t* shmobj) {
  if (shmobj->ptr) {
    A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(
        munmap(shmobj->ptr, shmobj->stat.st_size));
  }
  if (shmobj->fd) {
    A0_INTERNAL_RETURN_ERR_ON_MINUS_ONE(close(shmobj->fd));
  }
  return A0_OK;
}
