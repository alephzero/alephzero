/**
 * \file file.h
 * \rst
 *
 * File
 * ---------
 *
 * A file, in the AlephZero context, wraps a normal linux file.
 *
 * On **open**, the path and file are automatically created.
 * The file is opened and mmap-ed to provide an **arena**.
 *
 * The given path is relative to **\/dev/shm**. This can be overriden
 * by setting the environmental variable **A0_ROOT**.
 *
 * The path may also be given as an absolute path (starting with **\/**).
 *
 * Tilde '**~**' is not expanded.
 *
 * \endrst
 */

#ifndef A0_FILE_H
#define A0_FILE_H

#include <a0/common.h>

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
typedef struct a0_file_creation_options_s {
  /// File size.
  off_t size;
  /// File mode.
  mode_t mode;
  /// Mode for directories that will be created as part of file creation.
  mode_t dir_mode;
} a0_file_creation_options_t;

/// Default file creation options.
///
/// 16MB and universal read+write.
extern const a0_file_creation_options_t A0_FILE_CREATION_OPTIONS_DEFAULT;

/// File object.
typedef struct a0_file_s {
  const char* path;
  int fd;
  stat_t stat;
  a0_arena_t arena;
} a0_file_t;

/// Open a file at the given path.
///
/// If the file does not exist, it will be created automatically.
///
/// ::A0_FILE_CREATION_OPTIONS_DEFAULT is used if a0_file_creation_options_t is NULL.
///
/// The file is zero-ed out when first opened.
errno_t a0_file_open(const char* path, const a0_file_creation_options_t*, a0_file_t* out);

/// Closes a file. The file still exists.
errno_t a0_file_close(a0_file_t*);

/// Removes the specified file.
errno_t a0_file_remove(const char* path);
/// Removes the specified file or directory, including all subdirectories.
errno_t a0_file_remove_all(const char* path);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif  // A0_FILE_H
