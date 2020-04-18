#ifndef A0_SHM_H
#define A0_SHM_H

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
  a0_buf_t buf;
} a0_shm_t;

// Note: A0_SHM_OPTIONS_DEFAULT is used if a0_shm_options_t is NULL.
// Note: ftruncate is used to resize the file. This guarantees the file is
//       zero-ed out.
errno_t a0_shm_open(const char* path, const a0_shm_options_t*, a0_shm_t* out);
errno_t a0_shm_unlink(const char* path);
errno_t a0_shm_close(a0_shm_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SHM_H
