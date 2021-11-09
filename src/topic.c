#include <a0/empty.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/topic.h>

#include <alloca.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

typedef struct a0_topic_template_match_info_s {
  const char* tmpl;
  size_t tmpl_len;
  const char* topic;
  size_t topic_len;
  size_t prefix_len;
  size_t suffix_len;
} a0_topic_template_match_info_t;

A0_STATIC_INLINE
a0_err_t a0_topic_match_info(const char* tmpl,
                             const char* topic,
                             a0_topic_template_match_info_t* info) {
  if (!tmpl || !topic || !*topic || topic[0] == '/') {
    return A0_ERR_BAD_TOPIC;
  }
  const char* topic_ptr = strstr(tmpl, "{topic}");
  if (!topic_ptr) {
    return A0_ERR_BAD_TOPIC;
  }

  info->tmpl = tmpl;
  info->tmpl_len = strlen(tmpl);
  info->topic = topic;
  info->topic_len = strlen(topic);
  info->prefix_len = topic_ptr - tmpl;
  info->suffix_len = info->tmpl_len - info->prefix_len - strlen("{topic}");

  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_topic_write_path(a0_topic_template_match_info_t info,
                             char* write_ptr) {
  memcpy(write_ptr, info.tmpl, info.prefix_len);
  write_ptr += info.prefix_len;
  memcpy(write_ptr, info.topic, info.topic_len);
  write_ptr += info.topic_len;
  memcpy(write_ptr, info.tmpl + info.prefix_len + strlen("{topic}"), info.suffix_len);
  write_ptr += info.suffix_len;
  *write_ptr = '\0';
  return A0_OK;
}

a0_err_t a0_topic_path(const char* tmpl,
                       const char* topic,
                       const char** path) {
  a0_topic_template_match_info_t info = A0_EMPTY;
  A0_RETURN_ERR_ON_ERR(a0_topic_match_info(tmpl, topic, &info));
  char* mut_path = (char*)malloc(info.prefix_len + info.topic_len + info.suffix_len + 1);
  a0_topic_write_path(info, mut_path);
  *path = mut_path;
  return A0_OK;
}

a0_err_t a0_topic_open(const char* tmpl,
                       const char* topic,
                       const a0_file_options_t* topic_opts,
                       a0_file_t* file) {
  a0_topic_template_match_info_t info = A0_EMPTY;
  A0_RETURN_ERR_ON_ERR(a0_topic_match_info(tmpl, topic, &info));
  char* path = (char*)alloca(info.prefix_len + info.topic_len + info.suffix_len + 1);
  a0_topic_write_path(info, path);
  return a0_file_open(path, topic_opts, file);
}
