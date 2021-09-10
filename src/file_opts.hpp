#pragma once

#include <a0/file.h>
#include <a0/file.hpp>

namespace a0 {
namespace {  // NOLINT(google-build-namespaces)

inline a0_file_options_t c_fileopts(File::Options opts) {
  return a0_file_options_t{
      .create_options = {
          .size = opts.create_options.size,
          .mode = opts.create_options.mode,
          .dir_mode = opts.create_options.dir_mode,
      },
      .open_options = {
          .arena_mode = opts.open_options.arena_mode,
      },
  };
}

}  // namespace
}  // namespace a0
