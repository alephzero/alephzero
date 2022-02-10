#ifndef A0_ENV_H
#define A0_ENV_H

#include <a0/buf.h>
#include <a0/err.h>
#include <a0/inline.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* a0_env_root();
const char* a0_env_topic();

const char* a0_env_topic_tmpl_cfg();
const char* a0_env_topic_tmpl_deadman();
const char* a0_env_topic_tmpl_log();
const char* a0_env_topic_tmpl_prpc();
const char* a0_env_topic_tmpl_pubsub();
const char* a0_env_topic_tmpl_rpc();

#ifdef __cplusplus
}
#endif

#endif  // A0_ENV_H
