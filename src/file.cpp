#include <a0/file.hpp>

#include "c_wrap.hpp"

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
  a0_file_options_t c_opts{
      .create_options = {
          .size = opts.create_options.size,
          .mode = opts.create_options.mode,
          .dir_mode = opts.create_options.dir_mode,
      },
      .open_options = {
          .arena_mode = opts.open_options.arena_mode,
      },
  };
  set_c(
      &c,
      [&](a0_file_t* c) {
        return a0_file_open(path.data(), &c_opts, c);
      },
      a0_file_close);
}

File::operator const Buf() const {
  return (Buf)(Arena)*this;
}

File::operator Buf() {
  return (Buf)(Arena)*this;
}

File::operator const Arena() const {
  CHECK_C;
  auto save = c;
  return make_cpp<Arena>(
      [&](a0_arena_t* arena) {
        *arena = c->arena;
        return A0_OK;
      },
      [save](a0_arena_t*) {});
}

File::operator Arena() {
  return as_mutable((Arena)as_const(*this));
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
  if (err == ENOENT) {
    return;
  }
  check(err);
}

void File::remove_all(string_view path) {
  check(a0_file_remove_all(path.data()));
}

}  // namespace a0
