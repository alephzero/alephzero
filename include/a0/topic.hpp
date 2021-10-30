#pragma once

#include <a0/file.hpp>
#include <a0/inline.h>
#include <a0/string_view.hpp>

namespace a0 {

std::string topic_path(string_view tmpl, string_view topic);
File topic_open(string_view tmpl, string_view topic, File::Options opts);

}  // namespace a0
