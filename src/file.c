#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "err_macro.h"

#ifdef DEBUG
#include "assert.h"
#include "ref_cnt.h"
#endif

A0_STATIC_INLINE
a0_err_t a0_mkdir(const char* path, mode_t mode) {
  stat_t st;
  if (stat(path, &st)) {
    if (mkdir(path, mode) && errno != EEXIST) {
      return A0_MAKE_SYSERR(errno);
    }
  } else if (!S_ISDIR(st.st_mode)) {
    return A0_MAKE_SYSERR(ENOTDIR);
  }

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_mkpath(const char* path, mode_t mode) {
  char* path_copy = strdup(path);

  a0_err_t err = A0_OK;
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
a0_err_t a0_joinpath(const char* dir, const char* fil, char** out) {
  size_t dir_size = strlen(dir);
  size_t fil_size = strlen(fil);

  if (!dir_size || !fil_size) {
    return A0_MAKE_SYSERR(ENOENT);
  }

  *out = (char*)malloc(dir_size + 1 + fil_size + 1);
  memcpy(*out, dir, dir_size);
  (*out)[dir_size] = '/';
  memcpy(*out + (dir_size + 1), fil, fil_size);
  (*out)[dir_size + 1 + fil_size] = '\0';

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_abspath(const char* rel, char** out) {
  if (!rel || !*rel) {
    return A0_MAKE_SYSERR(ENOENT);
  }

  if (rel[0] == '/') {
    *out = strdup(rel);
    return A0_OK;
  }

  const char* root = a0_env_root();
  if (!*root) {
    return A0_MAKE_SYSERR(ENOENT);
  }

  if (root[0] != '/') {
    return A0_MAKE_SYSERR(ENOENT);
  }

  return a0_joinpath(root, rel, out);
}

A0_STATIC_INLINE
a0_err_t a0_open(const char* path, a0_file_open_options_t opts, a0_file_t* file) {
  int flags = O_RDWR;
  if (opts.arena_mode == A0_ARENA_MODE_READONLY) {
    flags = O_RDONLY;
  }

  int fd = open(path, flags);
  A0_RETURN_SYSERR_ON_MINUS_ONE(fd);

  a0_err_t err = A0_OK;
  if (fstat(fd, &file->stat) == -1) {
    err = A0_MAKE_SYSERR(errno);
    close(fd);
  } else {
    file->path = path;
    file->fd = fd;
  }
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mktmp(const char* dir, a0_file_create_options_t opts, a0_file_t* file) {
  char* path;
  A0_RETURN_ERR_ON_ERR(a0_joinpath(dir, ".alephzero_mkstemp.XXXXXX", &path));

  file->fd = mkstemp(path);
  if (file->fd == -1) {
    a0_err_t err = A0_MAKE_SYSERR(errno);
    free(path);
    return err;
  }

  file->path = path;

  if (fchmod(file->fd, opts.mode) == -1 ||
      ftruncate(file->fd, opts.size) == -1 ||
      fstat(file->fd, &file->stat) == -1) {
    a0_err_t err = A0_MAKE_SYSERR(errno);

    close(file->fd);
    file->fd = -1;

    free(path);
    file->path = NULL;

    return err;
  }

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_tmp_move(a0_file_t* file, const char* path) {
  a0_err_t err = A0_OK;
  if (link(file->path, path) == -1) {
    err = A0_MAKE_SYSERR(errno);
  }
  unlink(file->path);
  free((char*)file->path);
  file->path = path;
  return err;
}

A0_STATIC_INLINE
a0_err_t a0_mmap(a0_file_t* file, const a0_file_open_options_t* open_options) {
  file->arena.mode = open_options->arena_mode;
  file->arena.buf.size = file->stat.st_size;

  int mmap_flags = MAP_SHARED;
  if (open_options->arena_mode == A0_ARENA_MODE_READONLY) {
    mmap_flags = MAP_PRIVATE;
  }
  mmap_flags |= MAP_FIXED_NOREPLACE;

  file->arena.buf.data = (uint8_t*)mmap(
      /* addr   = */ (void*)open_options->local_address,
      /* len    = */ file->arena.buf.size,
      /* prot   = */ PROT_READ | PROT_WRITE,
      /* flags  = */ mmap_flags,
      /* fd     = */ file->fd,
      /* offset = */ 0);
  if ((intptr_t)file->arena.buf.data == -1) {
    file->arena.buf = (a0_buf_t)A0_EMPTY;
    return A0_MAKE_SYSERR(errno);
  }

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_munmap(a0_file_t* file) {
  if (!file->arena.buf.data) {
    return A0_MAKE_SYSERR(EBADF);
  }

  A0_RETURN_SYSERR_ON_MINUS_ONE(munmap(file->arena.buf.data, file->arena.buf.size));
  file->arena.buf = (a0_buf_t)A0_EMPTY;

  return A0_OK;
}

const a0_file_options_t A0_FILE_OPTIONS_DEFAULT = {
    .create_options = {
        // 16MB.
        .size = 16 * 1024 * 1024,
        // Global read+write.
        .mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH,
        // Global read+write+execute.
        .dir_mode = S_IRWXU | S_IRWXG | S_IRWXO,
    },
    .open_options = {
        .arena_mode = A0_ARENA_MODE_SHARED,
        .local_address = 0,
    },
};

A0_STATIC_INLINE
a0_err_t a0_do_open(
    a0_file_t* file,
    const char* path,
    const a0_file_options_t* opts) {
  char* path_copy = NULL;
  char* dir = NULL;

  a0_err_t err;
  while (true) {
    err = a0_open(path, opts->open_options, file);
    if (!err) {
      err = a0_mmap(file, &opts->open_options);
      if (err) {
        close(file->fd);
        file->fd = 0;
        file->path = NULL;
      }
      break;
    } else if (A0_SYSERR(err) != ENOENT) {
      break;
    }

    if (!path_copy) {
      path_copy = strdup(path);
      dir = dirname(path_copy);
    }

    err = a0_mktmp(dir, opts->create_options, file);
    if (err) {
      break;
    }
    err = a0_mmap(file, &opts->open_options);
    if (err) {
      close(file->fd);
      free((void*)file->path);
      file->fd = 0;
      file->path = NULL;
      break;
    }

    err = a0_tmp_move(file, path);
    if (err) {
      close(file->fd);
      file->fd = 0;
      file->path = NULL;
    }
    if (A0_SYSERR(err) != EEXIST) {
      break;
    }
  }

  if (path_copy) {
    free(path_copy);
  }

  return err;
}

a0_err_t a0_file_open(
    const char* path,
    const a0_file_options_t* opts_,
    a0_file_t* out) {
  const a0_file_options_t* opts = opts_;
  if (!opts) {
    opts = &A0_FILE_OPTIONS_DEFAULT;
  }

  char* filepath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &filepath));
  a0_err_t err = a0_mkpath(filepath, opts->create_options.dir_mode);
  if (err) {
    free(filepath);
    return err;
  }

  err = a0_do_open(out, filepath, opts);
  if (err) {
    free(filepath);
    return err;
  }

#ifdef DEBUG
  a0_ref_cnt_inc(out->arena.buf.data, NULL);
#endif

  return A0_OK;  // NOLINT(clang-analyzer-unix.Malloc): false positive. filepath is owned by out.
}

a0_err_t a0_file_close(a0_file_t* file) {
  if (!file->path || !file->arena.buf.data) {
    return A0_MAKE_SYSERR(EBADF);
  }

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(file->arena.buf.data, NULL),
      "File reference count corrupt: %s",
      file->path);

  size_t cnt;
  a0_ref_cnt_get(file->arena.buf.data, &cnt);
  A0_ASSERT(
      cnt == 0,
      "File closing while still in use: %s",
      file->path);
#endif

  close(file->fd);
  file->fd = 0;

  free((void*)file->path);
  file->path = NULL;

  return a0_munmap(file);
}

a0_err_t a0_file_iter_init(a0_file_iter_t* iter, const char* path) {
  char* abspath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &abspath));

  iter->_path_len = strlen(abspath);
  if (iter->_path_len > PATH_MAX) {
    free(abspath);
    return A0_MAKE_SYSERR(ENAMETOOLONG);
  }

  memcpy(iter->_path, abspath, iter->_path_len + 1);
  free(abspath);

  if (iter->_path[iter->_path_len - 1] != '/') {
    iter->_path[iter->_path_len] = '/';
    iter->_path_len++;
  }

  stat_t stat;
  A0_RETURN_SYSERR_ON_MINUS_ONE(lstat(path, &stat));

  if (!S_ISDIR(stat.st_mode)) {
    return A0_MAKE_SYSERR(ENOTDIR);
  }

  iter->_dir = opendir(path);
  if (!iter->_dir) {
    return A0_MAKE_SYSERR(errno);
  }
  return A0_OK;
}

a0_err_t a0_file_iter_next(a0_file_iter_t* iter, a0_file_iter_entry_t* entry) {
  struct dirent* dir_entity;
  while ((dir_entity = readdir(iter->_dir)) && (!memcmp(dir_entity->d_name, ".", 2) || !memcmp(dir_entity->d_name, "..", 3))) {
  }
  if (!dir_entity) {
    return A0_ERR_ITER_DONE;
  }

  size_t len = strlen(dir_entity->d_name);

  entry->filename = dir_entity->d_name;
  entry->fullpath = iter->_path;
  entry->d_type = dir_entity->d_type;
  memcpy(iter->_path + iter->_path_len, dir_entity->d_name, len);
  iter->_path[iter->_path_len + len] = '\0';

  return A0_OK;
}

a0_err_t a0_file_iter_close(a0_file_iter_t* iter) {
  closedir(iter->_dir);
  return A0_OK;
}

a0_err_t a0_file_remove(const char* path) {
  char* abspath;
  A0_RETURN_ERR_ON_ERR(a0_abspath(path, &abspath));

  a0_err_t err = A0_OK;
  if (remove(abspath) == -1) {
    err = A0_MAKE_SYSERR(errno);
  }
  free(abspath);
  return err;
}

a0_err_t a0_file_remove_all(const char* path) {
  a0_file_iter_t iter;
  A0_RETURN_ERR_ON_ERR(a0_file_iter_init(&iter, path));

  a0_file_iter_entry_t entry;
  while (!a0_file_iter_next(&iter, &entry)) {
    if (entry.d_type == DT_DIR) {
      a0_file_remove_all(entry.fullpath);
    }
    a0_file_remove(entry.fullpath);
  }

  a0_err_t err = a0_file_remove(path);
  a0_file_iter_close(&iter);
  return err;
}
