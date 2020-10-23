#include "ref_cnt.h"

#include <a0/errno.h>

#include <cerrno>
#include <unordered_map>

#include "sync.hpp"

namespace {

a0::sync<std::unordered_map<void*, size_t>>& global_counts() {
  static a0::sync<std::unordered_map<void*, size_t>> counts;
  return counts;
}

}  // namespace

errno_t a0_ref_cnt_inc(void* key) {
  global_counts().with_lock([&](auto& cnts) {
    cnts[key]++;
  });
  return A0_OK;
}

errno_t a0_ref_cnt_dec(void* key) {
  return global_counts().with_lock([&](auto& cnts) {
    if (!cnts.count(key)) {
      return EINVAL;
    }
    cnts[key]--;
    if (cnts[key] == 0) {
      cnts.erase(key);
    }
    return A0_OK;
  });
}

errno_t a0_ref_cnt_get(void* key, size_t* out) {
  *out = 0;
  return global_counts().with_shared_lock([&](const auto& cnts) {
    auto it = cnts.find(key);
    if (it != cnts.end()) {
      *out = it->second;
    }
    return A0_OK;
  });
}
