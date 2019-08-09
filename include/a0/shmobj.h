#ifndef A0_SHMOBJ_H
#define A0_SHMOBJ_H

#include <a0/common.h>

#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stat stat_t;

typedef struct a0_shmobj_options_s {
  off_t size;
} a0_shmobj_options_t;

typedef struct a0_shmobj_s {
  int fd;
  stat_t stat;
  uint8_t* ptr;
} a0_shmobj_t;

// a0_shmobj_t may NOT be NULL.
// Note: a0_shmobj_options_t may be only by NULL if the file already exists.
errno_t a0_shmobj_open(const char* path, const a0_shmobj_options_t*, a0_shmobj_t* out);
errno_t a0_shmobj_unlink(const char* path);
errno_t a0_shmobj_close(a0_shmobj_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_SHMOBJ_H
