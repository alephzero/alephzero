#pragma once

#ifdef __cpp_lib_to_chars
#include <charconv>
namespace a0 {
namespace {

using std::to_chars;

}  // namespace
}  // namespace a0
#else
#include <string.h>
#include <string>
namespace a0 {
namespace {

// TODO: This is about 5x slower then std::to_chars.
inline void to_chars(char* start, char* end, uint64_t val) {
  (void)end;
  auto tmp = std::to_string(val);
  strcpy(start, tmp.c_str());
}

}  // namespace
}  // namespace a0
#endif
