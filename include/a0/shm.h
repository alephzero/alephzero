#ifndef A0_SHMOBJ_H
#define A0_SHMOBJ_H

#include <a0/common.h>

#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat stat_t;

typedef struct a0_shm_options_s {
  off_t size;
} a0_shm_options_t;

typedef struct a0_shm_s {
  const char* path;  // memcpy-ed in a0_shm_open.
  a0_buf_t buf;
} a0_shm_t;

// Note: a0_shm_options_t may be only by NULL if the file already exists.
errno_t a0_shm_open(const char* path, const a0_shm_options_t*, a0_shm_t* out);
errno_t a0_shm_unlink(const char* path);
errno_t a0_shm_close(a0_shm_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SHMOBJ_H
