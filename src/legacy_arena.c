#include <a0/arena.h>
#include <a0/common.h>
#include <a0/errno.h>
#include <a0/legacy_arena.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

  a0_file_options_t file_opts = A0_FILE_OPTIONS_DEFAULT;
  file_opts.create_options.size = opts->size;
  // NOTE: file does not support resize.

  if (!path || !path[0]) {
    return ENOENT;
  }
  for (size_t i = 1; path[i]; i++) {
    if (path[i] == '/') {
      return EINVAL;
    }
  }

  a0_buf_t full_path;
  FILE* ss = open_memstream((char**)&full_path.ptr, &full_path.size);
  if (path[0] == '/') {
    fprintf(ss, "/dev/shm%s%c", path, '\0');
  } else {
    fprintf(ss, "/dev/shm/%s%c", path, '\0');
  }
  fflush(ss);
  fclose(ss);

  errno_t err = a0_file_open((char*)full_path.ptr, &file_opts, &out->_file);
  free(full_path.ptr);
  A0_RETURN_ERR_ON_ERR(err);

  out->path = strdup(path);
  out->arena = out->_file.arena;

  return A0_OK;
}

errno_t a0_shm_unlink(const char* path) {
  a0_buf_t full_path;
  FILE* ss = open_memstream((char**)&full_path.ptr, &full_path.size);
  if (path[0] == '/') {
    fprintf(ss, "/dev/shm%s%c", path, '\0');
  } else {
    fprintf(ss, "/dev/shm/%s%c", path, '\0');
  }
  fflush(ss);
  fclose(ss);

  errno_t err = A0_OK;
  if (unlink((char*)full_path.ptr) == -1) {
    err = errno;
  }
  free(full_path.ptr);
  return err;
}

errno_t a0_shm_close(a0_shm_t* shm) {
  if (!shm->path) {
    return EBADF;
  }

  free((void*)shm->path);
  shm->path = NULL;

  return a0_file_close(&shm->_file);
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

  a0_file_options_t file_opts = A0_FILE_OPTIONS_DEFAULT;
  file_opts.create_options.size = opts->size;
  // NOTE: file does not support resize.

  if (!path || path[0] != '/') {
    return ENOENT;
  }

  A0_RETURN_ERR_ON_ERR(a0_file_open(path, &file_opts, &out->_file));

  out->path = strdup(path);
  out->arena = out->_file.arena;

  return A0_OK;
}

errno_t a0_disk_unlink(const char* path) {
  A0_RETURN_ERR_ON_MINUS_ONE(unlink(path));
  return A0_OK;
}

errno_t a0_disk_close(a0_disk_t* disk) {
  if (!disk->path) {
    return EBADF;
  }

  free((void*)disk->path);
  disk->path = NULL;

  return a0_file_close(&disk->_file);
}
