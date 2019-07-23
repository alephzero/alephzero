#include <a0/shmobj.h>

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

errno_t a0_shmobj_stat(const char* path, stat_t* out_stat) {
  int fd = shm_open(path, O_RDONLY, 0);
  if (fd == -1) {
    return errno;
  }

  errno_t err = A0_OK;
  if (out_stat && fstat(fd, out_stat) == -1) {
    err = errno;
  }
  close(fd);

  return err;
}

errno_t a0_shmobj_exists(const char* path, bool* out_exists) {
  errno_t err = a0_shmobj_stat(path, NULL);
  *out_exists = (err == A0_OK);
  if (err == ENOENT) {
    return A0_OK;
  }
  return err;
}

errno_t a0_shmobj_create(const char* path, const a0_shmobj_options_t* opts) {
  int fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO);
  if (fd == -1) {
    return errno;
  }
  ftruncate(fd, opts->size);

  close(fd);

  return A0_OK;
}

errno_t a0_shmobj_destroy(const char* path) {
  shm_unlink(path);
  return A0_OK;
}

errno_t a0_shmobj_attach(const char* path, a0_shmobj_t* out) {
  _A0_RETURN_ERR_ON_ERR(a0_shmobj_stat(path, &out->stat));

  out->fd = shm_open(path, O_RDWR, 0);
  out->ptr = (uint8_t*)mmap(
      /* addr   = */ 0,
      /* length = */ out->stat.st_size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ MAP_SHARED,
      /* fd     = */ out->fd,
      /* offset = */ 0);

  return A0_OK;
}

errno_t a0_shmobj_detach(a0_shmobj_t* obj) {
  if (obj->ptr) {
    munmap(obj->ptr, obj->stat.st_size);
  }
  if (obj->fd) {
    close(obj->fd);
  }
  return A0_OK;
}

errno_t a0_shmobj_create_or_attach(const char* path, const a0_shmobj_options_t* opts, a0_shmobj_t* out) {
  bool exists;
  _A0_RETURN_ERR_ON_ERR(a0_shmobj_exists(path, &exists));
  if (!exists) {
    _A0_RETURN_ERR_ON_ERR(a0_shmobj_create(path, opts));
  }
  return a0_shmobj_attach(path, out);
}
