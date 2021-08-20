#pragma once

#include <a0/arena.hpp>
#include <a0/buf.hpp>
#include <a0/c_wrap.hpp>
#include <a0/file.h>
#include <a0/string_view.hpp>

#include <cstdint>

namespace a0 {

struct File : details::CppWrap<a0_file_t> {
  /// Options for creating new files or directories.
  ///
  /// These will not change existing files.
  struct Options {
    struct CreateOptions {
      /// File size.
      off_t size;
      /// File mode.
      mode_t mode;
      /// Mode for directories that will be created as part of file creation.
      mode_t dir_mode;
    } create_options;

    struct OpenOptions {
      /// ...
      a0_arena_mode_t arena_mode;
    } open_options;

    /// Default file creation options.
    ///
    /// 16MB and universal read+write.
    static Options DEFAULT;
  };

  File() = default;
  File(string_view path);
  File(string_view path, Options);

  /// Implicit conversions.
  operator const Buf() const;
  operator Buf();
  operator const Arena() const;
  operator Arena();

  /// File size.
  size_t size() const;
  /// File path.
  std::string path() const;

  /// File descriptor.
  int fd() const;
  /// File state.
  stat_t stat() const;

  /// Removes the specified file.
  static void remove(string_view path);
  /// Removes the specified file or directory, including all subdirectories.
  static void remove_all(string_view path);
};

}  // namespace a0
