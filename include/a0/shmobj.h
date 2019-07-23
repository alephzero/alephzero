#ifndef A0_SHMOBJ_H
#define A0_SHMOBJ_H

#include <a0/err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

typedef struct stat stat_t;

errno_t a0_shmobj_stat(const char* path, stat_t* out_stat);
errno_t a0_shmobj_exists(const char* path, bool* out_exists);

typedef struct a0_shmobj_options_s {
  off_t size;
} a0_shmobj_options_t;

errno_t a0_shmobj_create(const char* path, const a0_shmobj_options_t*);
errno_t a0_shmobj_destroy(const char* path);

typedef struct a0_shmobj_s {
  int fd;
  stat_t stat;
  uint8_t* ptr;
} a0_shmobj_t;

errno_t a0_shmobj_attach(const char* path, a0_shmobj_t* out);
errno_t a0_shmobj_detach(a0_shmobj_t*);

errno_t a0_shmobj_create_or_attach(const char* path, const a0_shmobj_options_t* opts, a0_shmobj_t* out);

#endif
