#include "topic.h"

#include <a0/buf.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/node.h>

#include <alloca.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_macro.h"

enum { A0_TOPIC_TMPL_MAX_MATCH_CNT = 4 };

typedef struct a0_topic_template_match_info_s {
  a0_buf_t segments[A0_TOPIC_TMPL_MAX_MATCH_CNT + 1];
  uint8_t num_segments;
  size_t topic_len;
  size_t path_len;
} a0_topic_template_match_info_t;

A0_STATIC_INLINE
a0_err_t a0_topic_match_info(const char* tmpl,
                             const char* topic,
                             a0_topic_template_match_info_t* info) {
  if (!tmpl || !*tmpl) {
    return A0_ERR_BAD_TOPIC;
  }
  if (!topic || !*topic || topic[0] == '/') {
    return A0_ERR_BAD_TOPIC;
  }

  info->topic_len = strlen(topic);

  info->path_len = 0;
  const char* prev = tmpl;
  const char* next;
  while ((next = strstr(prev, "{topic}")) && info->num_segments < A0_TOPIC_TMPL_MAX_MATCH_CNT) {
    size_t seg_len = next - prev;
    info->segments[info->num_segments++] = (a0_buf_t){
        .ptr = (uint8_t*)prev,
        .size = seg_len,
    };

    info->path_len += next - prev + info->topic_len;
    prev = next + strlen("{topic}");
  }
  size_t seg_len = strlen(prev);
  info->segments[info->num_segments++] = (a0_buf_t){
      .ptr = (uint8_t*)prev,
      .size = seg_len,
  };
  info->path_len += seg_len;
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_topic_write_path(const char* topic,
                             a0_topic_template_match_info_t info,
                             char* write_ptr) {
  memcpy(write_ptr, info.segments[0].ptr, info.segments[0].size);
  write_ptr += info.segments[0].size;
  for (size_t i = 1; i < info.num_segments; i++) {
    memcpy(write_ptr, topic, info.topic_len);
    write_ptr += info.topic_len;

    memcpy(write_ptr, info.segments[i].ptr, info.segments[i].size);
    write_ptr += info.segments[i].size;
  }
  *write_ptr = '\0';
  return A0_OK;
}

a0_err_t a0_topic_path(const char* tmpl,
                       const char* topic,
                       const char** path) {
  if (!topic || !*topic) {
    topic = a0_node();
  }
  a0_topic_template_match_info_t info = A0_EMPTY;
  A0_RETURN_ERR_ON_ERR(a0_topic_match_info(tmpl, topic, &info));

  char* path_mut = (char*)malloc(info.path_len + 1);
  a0_err_t err = a0_topic_write_path(topic, info, path_mut);
  if (err) {
    free(path_mut);
    return err;
  }
  *path = path_mut;
  return A0_OK;
}

a0_err_t a0_topic_open(const char* tmpl,
                       const char* topic,
                       const a0_file_options_t* topic_opts,
                       a0_file_t* file) {
  if (!topic || !*topic) {
    topic = a0_node();
  }
  a0_topic_template_match_info_t info = A0_EMPTY;
  A0_RETURN_ERR_ON_ERR(a0_topic_match_info(tmpl, topic, &info));

  char* path = (char*)alloca(info.path_len + 1);
  A0_RETURN_ERR_ON_ERR(a0_topic_write_path(topic, info, path));
  return a0_file_open(path, topic_opts, file);
}
