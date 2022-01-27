#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/pathglob.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

a0_err_t a0_abspath(const char* rel, char** out);

a0_err_t a0_pathglob_init(a0_pathglob_t* glob, const char* path_pattern) {
  *glob = (a0_pathglob_t)A0_EMPTY;
  if (!path_pattern) {
    return A0_ERR_BAD_PATH;
  }
  A0_RETURN_ERR_ON_ERR(a0_abspath(path_pattern, (char**)&glob->abspath.data));

  uint8_t* iter = glob->abspath.data;
  uint8_t* recent_slash = iter++;
  bool has_star = false;
  for (; *iter; iter++) {
    if (*iter == '*') {
      has_star = true;
    } else if (*iter == '/') {
      a0_pathglob_part_type_t type = A0_PATHGLOB_PART_TYPE_VERBATIM;
      if (has_star) {
        type = A0_PATHGLOB_PART_TYPE_PATTERN;
        if (iter == recent_slash + 3 && !memcmp(recent_slash, "/**", 3)) {
          type = A0_PATHGLOB_PART_TYPE_RECURSIVE;
        }
      }
      glob->parts[glob->depth++] = (a0_pathglob_part_t){
          (a0_buf_t){recent_slash + 1, iter - recent_slash - 1},
          type,
      };
      recent_slash = iter;
    }
  }
  a0_pathglob_part_type_t type = has_star ? A0_PATHGLOB_PART_TYPE_PATTERN : A0_PATHGLOB_PART_TYPE_VERBATIM;
  glob->parts[glob->depth++] = (a0_pathglob_part_t){
      (a0_buf_t){recent_slash + 1, iter - recent_slash - 1},
      type,
  };

  return A0_OK;
}

a0_err_t a0_pathglob_close(a0_pathglob_t* glob) {
  if (glob->abspath.data) {
    free(glob->abspath.data);
    glob->abspath.data = 0;
  }
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_pathglob_pattern_match(a0_pathglob_part_t part, a0_buf_t buf, bool* out) {
  uint8_t* star_g = NULL;
  uint8_t* star_r = NULL;

  uint8_t* glob = part.str.data;
  uint8_t* glob_end = glob + part.str.size;

  uint8_t* real = buf.data;
  uint8_t* real_end = real + buf.size;

  while (real != real_end) {
    if (*glob == '*') {
      star_g = ++glob;
      star_r = real;
      if (glob == glob_end) {
        *out = true;
        return A0_OK;
      }
    } else if (*real == *glob) {
      glob++;
      real++;
    } else {
      if (!star_g) {
        *out = false;
        return A0_OK;
      }
      real = ++star_r;
      glob = star_g;
    }
  }
  while (*glob == '*') {
    glob++;
  }
  *out = glob == glob_end;
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_pathglob_part_match(a0_pathglob_part_t globpart, a0_buf_t buf, bool* out) {
  switch (globpart.type) {
    case A0_PATHGLOB_PART_TYPE_VERBATIM: {
      *out = globpart.str.size == buf.size && !memcmp(globpart.str.data, buf.data, buf.size);
      return A0_OK;
    }
    case A0_PATHGLOB_PART_TYPE_PATTERN: {
      return a0_pathglob_pattern_match(globpart, buf, out);
    }
    case A0_PATHGLOB_PART_TYPE_RECURSIVE: {
      *out = true;
      return A0_OK;
    }
  }
  return A0_ERR_INVALID_ARG;
}

a0_err_t a0_pathglob_match(a0_pathglob_t* glob, const char* path, bool* out) {
  a0_pathglob_t real;
  A0_RETURN_ERR_ON_ERR(a0_pathglob_init(&real, path));

  size_t star_g = 0;
  size_t star_r = 0;

  size_t glob_idx = 0;
  size_t real_idx = 0;
  while (real_idx < real.depth) {
    if (glob_idx < glob->depth && glob->parts[glob_idx].type == A0_PATHGLOB_PART_TYPE_RECURSIVE) {
      star_g = ++glob_idx;
      star_r = real_idx;
      if (glob_idx == glob->depth && real_idx + 1 != real.depth) {
        *out = true;
        a0_pathglob_close(&real);
        return A0_OK;
      }
    } else {
      bool segment_match = false;
      if (glob_idx < glob->depth && (real_idx + 1 != real.depth || glob->parts[glob_idx].type != A0_PATHGLOB_PART_TYPE_RECURSIVE)) {
        A0_RETURN_ERR_ON_ERR(a0_pathglob_part_match(glob->parts[glob_idx], real.parts[real_idx].str, &segment_match));
      }
      if (segment_match) {
        real_idx++;
        glob_idx++;
      } else {
        if (!star_g) {
          *out = false;
          a0_pathglob_close(&real);
          return A0_OK;
        }
        real_idx = ++star_r;
        glob_idx = star_g;
      }
    }
  }
  for (; glob_idx < glob->depth; glob_idx++) {
    if (glob->parts[glob_idx].type != A0_PATHGLOB_PART_TYPE_RECURSIVE) {
      *out = false;
      a0_pathglob_close(&real);
      return A0_OK;
    }
  }
  *out = true;
  a0_pathglob_close(&real);
  return A0_OK;
}
