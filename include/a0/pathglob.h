#ifndef A0_PATHGLOB_H
#define A0_PATHGLOB_H

#include <a0/buf.h>
#include <a0/err.h>

#include <stdbool.h>  // IWYU pragma: keep
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { A0_PATHGLOB_MAX_DEPTH = 32 };

typedef enum a0_pathglob_part_type_e {
  A0_PATHGLOB_PART_TYPE_VERBATIM,
  A0_PATHGLOB_PART_TYPE_PATTERN,
  A0_PATHGLOB_PART_TYPE_RECURSIVE,
} a0_pathglob_part_type_t;

typedef struct a0_pathglob_part_s {
  a0_buf_t str;
  a0_pathglob_part_type_t type;
} a0_pathglob_part_t;

typedef struct a0_pathglob_s {
  a0_buf_t abspath;
  a0_pathglob_part_t parts[A0_PATHGLOB_MAX_DEPTH];
  size_t depth;
} a0_pathglob_t;

a0_err_t a0_pathglob_init(a0_pathglob_t*, const char* path_pattern);
a0_err_t a0_pathglob_close(a0_pathglob_t*);

a0_err_t a0_pathglob_match(a0_pathglob_t*, const char* path, bool* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_PATHGLOB_H
