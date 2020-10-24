#include <a0/common.h>
#include <a0/errno.h>
#include <a0/legacy_arena.h>

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

#ifdef DEBUG
#include "ref_cnt.h"
#endif

A0_STATIC_INLINE
errno_t a0_mmap(int fd, off_t size, bool resize, a0_arena_t* arena) {
  A0_RETURN_ERR_ON_MINUS_ONE(fd);

  stat_t stat;
  A0_RETURN_ERR_ON_MINUS_ONE(fstat(fd, &stat));

  arena->size = stat.st_size;
  if ((resize || !stat.st_size) && size != stat.st_size) {
    A0_RETURN_ERR_ON_MINUS_ONE(ftruncate(fd, size));
    arena->size = size;
  }

  arena->ptr = (uint8_t*)mmap(
      /* addr   = */ 0,
      /* len    = */ arena->size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ MAP_SHARED,
      /* fd     = */ fd,
      /* offset = */ 0);
  if (A0_UNLIKELY((intptr_t)arena->ptr == -1)) {
    arena->ptr = NULL;
    arena->size = 0;
    return errno;
  }

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_munmap(a0_arena_t* arena) {
  if (!arena->ptr) {
    return EBADF;
  }

  A0_RETURN_ERR_ON_MINUS_ONE(munmap(arena->ptr, arena->size));
  arena->ptr = NULL;
  arena->size = 0;

  return A0_OK;
}

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

  errno_t err = a0_mmap(fd, opts->size, opts->resize, &out->arena);
  if (fd > 0) {
    close(fd);
  }

  if (err) {
    return err;
  }

  out->path = strdup(path);

#ifdef DEBUG
  a0_ref_cnt_inc(out->arena.ptr);
#endif

  return A0_OK;
}

errno_t a0_shm_unlink(const char* path) {
  A0_RETURN_ERR_ON_MINUS_ONE(shm_unlink(path));
  return A0_OK;
}

errno_t a0_shm_close(a0_shm_t* shm) {
  if (!shm->path || !shm->arena.ptr) {
    return EBADF;
  }

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(shm->arena.ptr),
      "Shared memory file reference count corrupt: %s",
      shm->path);

  size_t cnt;
  a0_ref_cnt_get(shm->arena.ptr, &cnt);
  A0_ASSERT(
      cnt == 0,
      "Shared memory file closing while still in use: %s",
      shm->path);
#endif

  free((void*)shm->path);
  shm->path = NULL;

  return a0_munmap(&shm->arena);
}

const a0_disk_options_t A0_DISK_OPTIONS_DEFAULT = {
    .size = 16 * 1024 * 1024,
    .resize = false,
};

errno_t a0_disk_open(const char* path, const a0_disk_options_t* opts_, a0_disk_t* out) {
  const a0_disk_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_DISK_OPTIONS_DEFAULT;
  }

  int fd = open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);

  errno_t err = a0_mmap(fd, opts->size, opts->resize, &out->arena);
  if (fd > 0) {
    close(fd);
  }

  if (err) {
    return err;
  }

  out->path = strdup(path);

#ifdef DEBUG
  a0_ref_cnt_inc(out->arena.ptr);
#endif

  return A0_OK;
}

errno_t a0_disk_unlink(const char* path) {
  A0_RETURN_ERR_ON_MINUS_ONE(unlink(path));
  return A0_OK;
}

errno_t a0_disk_close(a0_disk_t* disk) {
  if (!disk->path || !disk->arena.ptr) {
    return EBADF;
  }

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(disk->arena.ptr),
      "Disk file reference count corrupt: %s",
      disk->path);

  size_t cnt;
  a0_ref_cnt_get(disk->arena.ptr, &cnt);
  A0_ASSERT(
      cnt == 0,
      "Disk file closing while still in use: %s",
      disk->path);
#endif

  free((void*)disk->path);
  disk->path = NULL;

  return a0_munmap(&disk->arena);
}
