#include <a0/inline.h>

#include <stdlib.h>

A0_STATIC_INLINE
const char* envdef(const char* name, const char* def) {
  const char* val = getenv(name);
  return val ? val : def;
}

const char* a0_env_root() {
  return envdef("A0_ROOT", "/dev/shm/alephzero");
}
const char* a0_env_topic() {
  return getenv("A0_TOPIC");
}

const char* a0_env_topic_tmpl_cfg() {
  return envdef("A0_TOPIC_TMPL_CFG", "{topic}.cfg.a0");
}
const char* a0_env_topic_tmpl_deadman() {
  return envdef("A0_TOPIC_TMPL_DEADMAN", "{topic}.deadman");
}
const char* a0_env_topic_tmpl_log() {
  return envdef("A0_TOPIC_TMPL_LOG", "{topic}.log.a0");
}
const char* a0_env_topic_tmpl_prpc() {
  return envdef("A0_TOPIC_TMPL_PRPC", "{topic}.prpc.a0");
}
const char* a0_env_topic_tmpl_pubsub() {
  return envdef("A0_TOPIC_TMPL_PUBSUB", "{topic}.pubsub.a0");
}
const char* a0_env_topic_tmpl_rpc() {
  return envdef("A0_TOPIC_TMPL_RPC", "{topic}.rpc.a0");
}
