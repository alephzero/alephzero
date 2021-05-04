#ifndef A0_SRC_PROTOCOL_UTIL_H
#define A0_SRC_PROTOCOL_UTIL_H

#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>

#include <alloca.h>

#include "err_util.h"

A0_STATIC_INLINE
errno_t a0_open_topic(const char* ext,
                      const char* topic,
                      const a0_file_options_t* topic_opts,
                      a0_file_t* file) {
  if (topic[0] == '\0' || topic[0] == '/') {
    return EINVAL;
  }

  const char* DIR = "alephzero/";
  const char* DOT = ".";
  const char* A0_EXT = "a0";
  char* path = (char*)alloca(strlen(DIR) +
                             strlen(topic) +
                             strlen(DOT) +
                             strlen(ext) +
                             strlen(DOT) +
                             strlen(A0_EXT) +
                             1);

  size_t off = 0;
  memcpy(path + off, DIR, strlen(DIR));
  off += strlen(DIR);
  memcpy(path + off, topic, strlen(topic));
  off += strlen(topic);
  memcpy(path + off, DOT, strlen(DOT));
  off += strlen(DOT);
  memcpy(path + off, ext, strlen(ext));
  off += strlen(ext);
  memcpy(path + off, DOT, strlen(DOT));
  off += strlen(DOT);
  memcpy(path + off, A0_EXT, strlen(A0_EXT));
  off += strlen(A0_EXT);
  path[off] = '\0';

  return a0_file_open(path, topic_opts, file);
}

#endif  // A0_SRC_PROTOCOL_UTIL_H
