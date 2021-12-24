#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/buf.hpp>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/file.hpp>
#include <a0/string_view.hpp>

#include <cerrno>
#include <cstddef>
#include <memory>
#include <string>

#include "c_opts.hpp"
#include "c_wrap.hpp"
#include "err_macro.h"

namespace a0 {

File::Options File::Options::DEFAULT = {
    .create_options = {
        .size = A0_FILE_OPTIONS_DEFAULT.create_options.size,
        .mode = A0_FILE_OPTIONS_DEFAULT.create_options.mode,
        .dir_mode = A0_FILE_OPTIONS_DEFAULT.create_options.dir_mode,
    },
    .open_options = {
        .arena_mode = A0_FILE_OPTIONS_DEFAULT.open_options.arena_mode,
    },
};

File::File(string_view path)
    : File(path, Options::DEFAULT) {}

File::File(string_view path, Options opts) {
  set_c(
      &c,
      [&](a0_file_t* c) {
        auto c_opts = c_fileopts(opts);
        return a0_file_open(path.data(), &c_opts, c);
      },
      a0_file_close);
}

File::operator Buf() const {
  return buf();
}

File::operator Arena() const {
  return arena();
}

Buf File::buf() const {
  return arena().buf();
}

Arena File::arena() const {
  CHECK_C;
  auto save = c;
  return make_cpp<Arena>(
      [&](a0_arena_t* arena) {
        *arena = c->arena;
        return A0_OK;
      },
      [save](a0_arena_t*) {});
}

size_t File::size() const {
  CHECK_C;
  return c->arena.buf.size;
}

std::string File::path() const {
  CHECK_C;
  return c->path;
}

int File::fd() const {
  CHECK_C;
  return c->fd;
}

stat_t File::stat() const {
  CHECK_C;
  return c->stat;
}

void File::remove(string_view path) {
  auto err = a0_file_remove(path.data());
  // Ignore "No such file or directory" errors.
  if (A0_SYSERR(err) == ENOENT) {
    return;
  }
  check(err);
}

void File::remove_all(string_view path) {
  check(a0_file_remove_all(path.data()));
}

}  // namespace a0
