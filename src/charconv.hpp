#pragma once

#ifdef __cpp_lib_to_chars
#include <charconv>
namespace a0 {
namespace {

template <typename T>
errno_t from_chars(const char* start, const char* end, T& val) {
  auto result = std::from_chars(start, end, val);
  if (result.ec != std::errc()) {
    return (errno_t)result.ec;
  }
  return A0_OK;
}

template <typename T>
errno_t to_chars(char* start, char* end, T val) {
  auto result = std::to_chars(start, end, val);
  if (result.ec != std::errc()) {
    return (errno_t)result.ec;
  }
  result.ptr = '\0';
  return A0_OK;
}

using std::from_chars;
using std::to_chars;

}  // namespace
}  // namespace a0
#else
#include <a0/common.h>
#include <string.h>
#include <string>

#include "macros.h"
namespace a0 {
namespace {

// TODO: This is about 4x slower then std::from_chars.
template <typename T>
errno_t from_chars(const char* start, const char* end, T& val) {
  (void)end;
  try {
    val = std::stoll(start);
    return A0_OK;
  } catch(...) {
    return EINVAL;
  }
}

// TODO: This is about 5x slower then std::to_chars.
template <typename T>
errno_t to_chars(char* start, char* end, T val) {
  (void)end;
  auto tmp = std::to_string(val);
  strcpy(start, tmp.c_str());
  return A0_OK;
}

}  // namespace
}  // namespace a0
#endif
