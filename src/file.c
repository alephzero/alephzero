// Necessary for nftw.
#define _XOPEN_SOURCE 500

#include <a0/common.h>
#include <a0/file.h>

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
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
errno_t a0_mkdir(const char* path, mode_t mode) {
  stat_t st;
  if (stat(path, &st)) {
    if (mkdir(path, mode) && errno != EEXIST) {
      return errno;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    return ENOTDIR;
  }

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_mkpath(const char* path, mode_t mode) {
  char* path_copy = strdup(path);

  errno_t err = A0_OK;
  char* scan_start = path_copy;
  char* scan_found;
  while (!err && (scan_found = strchr(scan_start, '/'))) {
    if (scan_found != scan_start) {
      *scan_found = '\0';
      err = a0_mkdir(path_copy, mode);
      *scan_found = '/';
    }
    scan_start = scan_found + 1;
  }

  free(path_copy);
  return err;
}

A0_STATIC_INLINE
errno_t a0_joinpath(const char* dir, const char* fil, char** out) {
  size_t dir_size = strlen(dir);
  size_t fil_size = strlen(fil);

  if (!dir_size || !fil_size) {
    return ENOENT;
  }

  *out = (char*)malloc(dir_size + 1 + fil_size + 1);
  memcpy(*out, dir, dir_size);
  (*out)[dir_size] = '/';
  memcpy(*out + (dir_size + 1), fil, fil_size);
  (*out)[dir_size + 1 + fil_size] = '\0';

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_abspath(const char* rel, char** out) {
  if (!rel || !*rel) {
    return ENOENT;
  }

  if (rel[0] == '/') {
    *out = strdup(rel);
    return A0_OK;
  }

  const char* root = getenv("A0_ROOT");
  if (!root) {
    root = "/dev/shm";
  }
  if (!*root) {
    return ENOENT;
  }

  if (root[0] != '/') {
    return ENOENT;
  }

  return a0_joinpath(root, rel, out);
}

A0_STATIC_INLINE
errno_t a0_create_or_connect(a0_file_t* file, const char* path, const a0_file_creation_options_t* opts) {
  errno_t err = A0_OK;
  char* path_copy = NULL;
  char* dir = NULL;
  char* tmppath = NULL;

connect:
  // Optimistically try to connect.
  file->fd = open(path, O_RDWR);
  if (A0_LIKELY(file->fd != -1)) {
    A0_FAIL_ON_MINUS_ONE(fstat(file->fd, &file->stat));
    return A0_OK;
  }

  // Make a file with another name. Set the mode and size. Move it to the final destination.
  path_copy = strdup(path);
  dir = dirname(path_copy);
  A0_FAIL_ON_MINUS_ONE(a0_joinpath(dir, ".alephzero_mkstemp.XXXXXX", &tmppath));

  file->fd = mkstemp(tmppath);
  A0_FAIL_ON_MINUS_ONE(file->fd);
  A0_FAIL_ON_MINUS_ONE(fchmod(file->fd, opts->mode));
  A0_FAIL_ON_MINUS_ONE(ftruncate(file->fd, opts->size));
  A0_FAIL_ON_MINUS_ONE(fstat(file->fd, &file->stat));
  if (rename(tmppath, path) == -1) {
    // Check for a race condition. Another process has already made the final file.
    if (errno == EEXIST) {
      close(file->fd);
      remove(tmppath);
      free(path_copy);
      path_copy = NULL;
      free(tmppath);
      tmppath = NULL;
      goto connect;
    }
    goto fail;
  }
  goto cleanup;

fail:
  err = errno;
  if (file->fd != -1) {
    close(file->fd);
  }
  file->fd = 0;
  if (tmppath) {
    remove(tmppath);
  }

cleanup:
  free(path_copy);
  free(tmppath);

  return err;
}

A0_STATIC_INLINE
errno_t a0_mmap(a0_file_t* file) {
  file->arena.size = file->stat.st_size;

  file->arena.ptr = (uint8_t*)mmap(
      /* addr   = */ 0,
      /* len    = */ file->arena.size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ MAP_SHARED,
      /* fd     = */ file->fd,
      /* offset = */ 0);
  if (A0_UNLIKELY((intptr_t)file->arena.ptr == -1)) {
    file->arena.ptr = NULL;
    file->arena.size = 0;
    return errno;
  }

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_munmap(a0_file_t* file) {
  if (!file->arena.ptr) {
    return EBADF;
  }

  A0_RETURN_ERR_ON_MINUS_ONE(munmap(file->arena.ptr, file->arena.size));
  file->arena.ptr = NULL;
  file->arena.size = 0;

  return A0_OK;
}

const a0_file_creation_options_t A0_FILE_CREATION_OPTIONS_DEFAULT = {
    // 16MB.
    .size = 16 * 1024 * 1024,
    // Global read+write.
    .mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
    // Global read+write+execute.
    .dir_mode = S_IRWXU | S_IRWXG | S_IRWXO,
};

errno_t a0_file_open(const char* path, const a0_file_creation_options_t* opts_, a0_file_t* out) {
  const a0_file_creation_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_FILE_CREATION_OPTIONS_DEFAULT;
  }

  char* filepath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &filepath));
  errno_t err = a0_mkpath(filepath, opts->dir_mode);
  if (err) {
    free(filepath);
    return err;
  }

  err = a0_create_or_connect(out, filepath, opts);
  if (err) {
    free(filepath);
    return err;
  }

  err = a0_mmap(out);
  if (err) {
    close(out->fd);
    free(filepath);
    return err;
  }

  out->path = filepath;

#ifdef DEBUG
  a0_ref_cnt_inc(out->arena.ptr);
#endif

  return A0_OK;
}

errno_t a0_file_close(a0_file_t* file) {
  if (!file->path || !file->arena.ptr) {
    return EBADF;
  }

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(file->arena.ptr),
      "File reference count corrupt: %s",
      file->path);

  size_t cnt;
  a0_ref_cnt_get(file->arena.ptr, &cnt);
  A0_ASSERT(
      cnt == 0,
      "File closing while still in use: %s",
      file->path);
#endif

  free((void*)file->path);
  file->path = NULL;

  return a0_munmap(file);
}

errno_t a0_file_remove(const char* path) {
  char* abspath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &abspath));

  errno_t err = A0_OK;
  if (unlink(abspath) == -1) {
    err = errno;
  }
  free(abspath);
  return err;
}

A0_STATIC_INLINE
int a0_nftw_remove_one(const char* subpath, const stat_t* st, int type, struct FTW* ftw) {
  (void)st;
  (void)type;
  (void)ftw;
  return remove(subpath);
}

errno_t a0_file_remove_all(const char* path) {
  char* abspath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &abspath));
  errno_t err = A0_OK;
  if (nftw(abspath, a0_nftw_remove_one, /* fd_limit */ 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS) == -1) {
    err = errno;
  }
  free(abspath);
  return err;
}