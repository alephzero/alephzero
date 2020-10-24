/**
 * \file arena.h
 * \rst
 *
 * An arena can be any contiguous memory buffer.
 *
 * For the transport, they must be zero-ed out before first initialization.
 *
 * File
 * ----
 *
 * A file is an arena within a normal linux file.
 *
 * On **open**, the path and file are automatically created.
 * The file is opened and mmap-ed to provide the **arena**.
 *
 * The path may be given as an absolute path (starting with a forward slash '/').
 *
 * The path may also be given as a relative path. By default, the path is relative
 * to **\/dev/shm**. This can be overriden by setting the environmental variable
 * **A0_ROOT**.
 *
 * **A0_ROOT** must be an absolute path. It is NOT relative to **\/dev/shm** or
 * your current directory.
 *
 *
 * Note that tilde '**~**' is not expanded.
 *
 * Files in **\/dev/shm**, or any other tmpfs directory will have lazily page allocation.
 * Large files are only an issue once the data is written to the files.
 *
 * Files are initially populated with null bytes.
 *
 * \endrst
 */

#ifndef A0_ARENA_H
#define A0_ARENA_H

#include <a0/common.h>
#include <a0/errno.h>

#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/// An arena can be any contiguous memory buffer.
typedef a0_buf_t a0_arena_t;

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
  /// Create a private copy-on-write mapping.
  /// Updates to the mapping are not visible to other processes mapping
  /// the same file, and are not carried through to the underlying file.
  /// It is unspecified whether changes made to the file are visible in
  /// the mapped region.
  bool readonly;
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
/// On open: read+write.
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
errno_t a0_file_open(
    const char* path,
    const a0_file_options_t* opt,
    a0_file_t* out);

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

#endif  // A0_ARENA_H
