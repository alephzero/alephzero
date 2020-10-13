#ifndef A0_FILE_H
#define A0_FILE_H

#include <a0/common.h>

#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat stat_t;

typedef struct a0_file_creation_options_s {
  off_t size;
  mode_t mode;
  mode_t dir_mode;
} a0_file_creation_options_t;

extern const a0_file_creation_options_t A0_FILE_CREATION_OPTIONS_DEFAULT;

typedef struct a0_file_s {
  const char* path;
  int fd;
  stat_t stat;
  a0_arena_t arena;
} a0_file_t;

// Open a file at the given path.
//
// If the file does not exist, it will be created automatically.
//
// A0_FILE_CREATION_OPTIONS_DEFAULT is used if a0_file_creation_options_t is NULL.
//
// During creation, ftruncate is used to size the file.
// This guarantees the file is zero-ed out when first opened.
errno_t a0_file_open(const char* path, const a0_file_creation_options_t*, a0_file_t* out);

// Closes a file. The file still exists.
errno_t a0_file_close(a0_file_t*);

// Removes the specified file or empty directory.
errno_t a0_file_remove(const char* path);
// Removes the specified file or directory, including all subdirectories.
errno_t a0_file_remove_all(const char* path);

#ifdef __cplusplus
}
#endif

#endif  // A0_FILE_H
