#include <a0/file.h>
#include <a0/file.hpp>
#include <a0/string_view.hpp>
#include <a0/topic.h>
#include <a0/topic.hpp>

#include <cstdlib>
#include <string>

#include "c_opts.hpp"
#include "c_wrap.hpp"

namespace a0 {

std::string topic_path(string_view tmpl, string_view topic) {
  const char* path;
  check(a0_topic_path(tmpl.data(), topic.data(), &path));
  std::string ret = path;
  free((void*)path);
  return ret;
}

File topic_open(string_view tmpl, string_view topic, File::Options opts) {
  return make_cpp<File>(
      [&](a0_file_t* c) {
        auto c_opts = c_fileopts(opts);
        return a0_topic_open(tmpl.data(), topic.data(), &c_opts, c);
      },
      a0_file_close);
}

}  // namespace a0
