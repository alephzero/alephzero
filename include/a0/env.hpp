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

}  // namespace env
}  // namespace a0
