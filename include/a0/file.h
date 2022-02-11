/**
 * \file file.h
 * \rst
 *
 * Create and Open
 * ---------------
 *
 * .. code-block:: cpp
 *
 *   a0::File file("path");
 *
 * The file will open, by default, **\/dev/shm/alephzero/path**.
 *
 * The **\/dev/shm/alephzero** comes from the **A0_ROOT** environment variable.
 * See below.
 *
 * You can also open that file with:
 *
 * .. code-block:: cpp
 *
 *   a0::File file("/dev/shm/alephzero/path");
 *
 * If the file doesn't exist, it will be created, along with any directories.
 *
 * If you want to set the size:
 *
 * .. code-block:: cpp
 *
 *   auto opts = a0::File::Options::DEFAULT;
 *   opts.create_options.size = 4 * 1024;
 *   a0::File file("path", opts);
 *
 * .. note::
 *
 *   **create_options** do not effect existing files.
 * 
 * **create_options** can also be used to set **mode**.
 *
 * Usage
 * -----
 *
 * .. code-block:: cpp
 *
 *   a0::File file("path");
 *
 *   a0::Arena arena = file;
 *   a0::Buf buf = file;
 *   file.size();
 *   file.path();  // absolute path
 *   file.fd();
 *   file.stat();  // at time of open
 *
 * Removing
 * --------
 *
 * .. code-block:: cpp
 *
 *   a0::File::remove("path");
 *   a0::File::remove_all("dir");  // recursive
 *
 * A0_ROOT
 * -------
 *
 * The **A0_ROOT** environment variable controls relative file paths.
 * It can be used to sandbox applications.
 *
 * **A0_ROOT** defaults to **\/dev/shm/alephzero/**
 *
 * **A0_ROOT** must be an absolute path (start with '/').
 * It is NOT relative to your current directory.
 * Tilde '**~**' is not expanded.
 *
 * \endrst
 */

#ifndef A0_FILE_H
#define A0_FILE_H

#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/err.h>

#include <dirent.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat stat_t;

/** \addtogroup FILE
 *  @{
 */

/// Options for creating new files or directories.
///
/// These will not change existing files.
typedef struct a0_file_create_options_s {
  /// File size.
  off_t size;
  /// File mode.
  mode_t mode;
  /// Mode for directories that will be created as part of file create.
  mode_t dir_mode;
} a0_file_create_options_t;

/// Options for opening files.
typedef struct a0_file_open_options_s {
  /// If SHARED or EXCLUSIVE, mmaps with MAP_SHARED.
  ///
  /// Updates to the mapping are visible to other processes mapping the
  /// same file.
  ///
  /// If READONLY, mmaps with MAP_PRIVATE.
  ///
  /// Updates to the mapping are not visible to other processes mapping
  /// the same file, and are not carried through to the underlying file.
  /// It is unspecified whether changes made to the file are visible in
  /// the mapped region.
  a0_arena_mode_t arena_mode;
} a0_file_open_options_t;

/// File options.
typedef struct a0_file_options_s {
  /// Create options.
  a0_file_create_options_t create_options;
  /// Open options.
  a0_file_open_options_t open_options;
} a0_file_options_t;

/// Default file options.
///
/// On create: 16MB and universal read+write.
///
/// On open: shared read+write.
extern const a0_file_options_t A0_FILE_OPTIONS_DEFAULT;

/// File object.
typedef struct a0_file_s {
  /// Absolute path to the file.
  const char* path;
  /// File descriptor.
  int fd;
  /// File stat (at time of open).
  stat_t stat;
  /// Arena mapping into the file.
  a0_arena_t arena;
} a0_file_t;

/// Open a file at the given path.
///
/// If the file does not exist, it will be created automatically.
///
/// ::A0_FILE_OPTIONS_DEFAULT is used if opt is NULL.
///
/// The file is zero-ed out when created.
a0_err_t a0_file_open(
    const char* path,
    const a0_file_options_t* opt,
    a0_file_t* out);

/// Closes a file. The file still exists.
a0_err_t a0_file_close(a0_file_t*);

typedef struct a0_file_iter_s {
  char _path[PATH_MAX + 1];
  size_t _path_len;
  DIR* _dir;
} a0_file_iter_t;

typedef struct a0_file_iter_entry_s {
  const char* fullpath;
  const char* filename;
  int d_type;
} a0_file_iter_entry_t;

a0_err_t a0_file_iter_init(a0_file_iter_t*, const char* path);
a0_err_t a0_file_iter_next(a0_file_iter_t*, a0_file_iter_entry_t*);
a0_err_t a0_file_iter_close(a0_file_iter_t*);

/// Removes the specified file.
a0_err_t a0_file_remove(const char* path);
/// Removes the specified file or directory, including all subdirectories.
a0_err_t a0_file_remove_all(const char* path);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_FILE_H
