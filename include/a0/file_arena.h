#ifndef A0_FILE_ARENA_H
#define A0_FILE_ARENA_H

#include <a0/common.h>

#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat stat_t;

typedef struct a0_shm_options_s {
  off_t size;
  bool resize;
} a0_shm_options_t;

extern const a0_shm_options_t A0_SHM_OPTIONS_DEFAULT;

typedef struct a0_shm_s {
  const char* path;  // memcpy-ed in a0_shm_open.
  a0_arena_t arena;
} a0_shm_t;

// Note: A0_SHM_OPTIONS_DEFAULT is used if a0_shm_options_t is NULL.
// Note: ftruncate is used to resize the file. This guarantees the file is
//       zero-ed out.
errno_t a0_shm_open(const char* path, const a0_shm_options_t*, a0_shm_t* out);
errno_t a0_shm_unlink(const char* path);
errno_t a0_shm_close(a0_shm_t*);

// TODO(lshamis): Maybe unify the shm/disk.

typedef struct a0_disk_options_s {
  off_t size;
  bool resize;
} a0_disk_options_t;

extern const a0_disk_options_t A0_DISK_OPTIONS_DEFAULT;

typedef struct a0_disk_s {
  const char* path;  // memcpy-ed in a0_disk_open.
  a0_arena_t arena;
} a0_disk_t;

// Note: A0_DISK_OPTIONS_DEFAULT is used if a0_disk_options_t is NULL.
// Note: ftruncate is used to resize the file. This guarantees the file is
//       zero-ed out.
errno_t a0_disk_open(const char* path, const a0_disk_options_t*, a0_disk_t* out);
errno_t a0_disk_unlink(const char* path);
errno_t a0_disk_close(a0_disk_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_FILE_ARENA_H
