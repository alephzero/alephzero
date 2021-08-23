#pragma once

#include <a0/file.h>
#include <a0/file.hpp>
#include <a0/inline.h>

namespace a0 {
namespace {

A0_STATIC_INLINE
a0_file_options_t c_fileopts(File::Options opts) {
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

}
}  // namespace a0
