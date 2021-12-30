#pragma once

#include <a0/file.h>
#include <a0/file.hpp>
#include <a0/reader.h>
#include <a0/reader.hpp>

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

inline a0_reader_options_t c_readeropts(Reader::Options opts) {
  return {
      .init = (a0_reader_init_t)opts.init,
      .iter = (a0_reader_iter_t)opts.iter,
  };
}

}  // namespace
}  // namespace a0
