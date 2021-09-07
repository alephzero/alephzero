#ifndef A0_SRC_CONFIG_COMMON_H
#define A0_SRC_CONFIG_COMMON_H

#include <a0/config.h>
#include <a0/inline.h>

#include "topic.h"

#ifdef __cplusplus
extern "C" {
#endif

A0_STATIC_INLINE
a0_err_t a0_config_topic_open(a0_config_topic_t topic, a0_file_t* out) {
  const char* tmpl = getenv("A0_CONFIG_TOPIC_TEMPLATE");
  if (!tmpl) {
    tmpl = "alephzero/{topic}.cfg.a0";
  }
  return a0_topic_open(tmpl, topic.name, topic.file_opts, out);
}

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_CONFIG_COMMON_H
