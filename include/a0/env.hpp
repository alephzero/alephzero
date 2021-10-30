#pragma once

#include <a0/env.h>

namespace a0 {
namespace env {

A0_STATIC_INLINE
const char* root() {
  return a0_env_root();
}
A0_STATIC_INLINE
const char* topic() {
  return a0_env_topic();
}

A0_STATIC_INLINE
const char* topic_tmpl_cfg() {
  return a0_env_topic_tmpl_cfg();
}
A0_STATIC_INLINE
const char* topic_tmpl_log() {
  return a0_env_topic_tmpl_log();
}
A0_STATIC_INLINE
const char* topic_tmpl_prpc() {
  return a0_env_topic_tmpl_prpc();
}
A0_STATIC_INLINE
const char* topic_tmpl_pubsub() {
  return a0_env_topic_tmpl_pubsub();
}
A0_STATIC_INLINE
const char* topic_tmpl_rpc() {
  return a0_env_topic_tmpl_rpc();
}

}  // namespace env
}  // namespace a0
