#pragma once

#define A0_CPP_17 (__cplusplus >= 201700L)

#include <a0/buf.h>
#include <a0/compare.h>
#include <a0/inline.h>
#include <a0/unused.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ios>
#include <iterator>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>

#if A0_CPP_17
#include <string_view>
#endif

namespace a0 {

class string_view {
  const char* data_{nullptr};
  size_t size_{0};

 public:
  using char_type = char;
  using traits_type = std::char_traits<char>;
  using size_type = size_t;

  using value_type = char;
  using reference = char&;
  using const_reference = const char&;
  using pointer = char*;
  using const_pointer = const char*;

  using iterator = const char*;
  using const_iterator = const char*;
  using reverse_iterator = std::reverse_iterator<const char*>;
  using const_reverse_iterator = std::reverse_iterator<const char*>;

  static const size_t npos = size_t(-1);

  explicit operator std::string() const { return std::string(data_, size_); }

  string_view() noexcept = default;
  string_view(const string_view& other) noexcept = default;
  string_view(const char* s, size_t count) noexcept
      : data_{s}, size_{count} {}
  string_view(const char* s) noexcept  // NOLINT(google-explicit-constructor)
      : string_view(s, strlen(s)) {}
  string_view(const std::string& str) noexcept  // NOLINT(google-explicit-constructor)
      : data_{str.c_str()}, size_{str.size()} {}

#if A0_CPP_17
  string_view(std::string_view str) noexcept  // NOLINT(google-explicit-constructor)
      : data_{str.data()}, size_{str.size()} {}

  operator std::string_view() const noexcept {
    return std::string_view(data_, size_);
  }
#endif  // A0_CPP_17

  string_view& operator=(const string_view& view) = default;

  const_iterator begin() const noexcept { return data_; }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator end() const noexcept { return data_ + size_; }
  const_iterator cend() const noexcept { return end(); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  const_reference at(size_type pos) const {
    if (pos >= size_) {
      throw std::out_of_range("string_view::at out of range");
    }
    return data_[pos];
  }
  const_reference operator[](size_type pos) const noexcept { return data_[pos]; }
  const_reference front() const noexcept { return *data_; }
  const_reference back() const noexcept { return data_[size_ - 1]; }
  const_pointer data() const noexcept { return data_; }

  size_type size() const noexcept { return size_; }
  size_type length() const noexcept { return size_; }
  static size_type max_size() noexcept { return npos - 1; }
  bool empty() const noexcept { return !size_; }

  void remove_prefix(size_type n) noexcept {
    data_ += n;
    size_ -= n;
  }
  void remove_suffix(size_type n) noexcept { size_ -= n; }
  void swap(string_view& v) noexcept {
    std::swap(data_, v.data_);
    std::swap(size_, v.size_);
  }

  size_type copy(char_type* dest, size_type count, size_type pos = 0) const {
    if (pos >= size_) {
      throw std::out_of_range("string_view::copy out of range");
    }
    size_t rcount = std::min(size_ - pos, count + 1);
    memcpy(dest, data_ + pos, rcount);
    return rcount;
  }
  string_view substr(size_type pos = 0, size_type len = npos) const {
    if (pos >= size_) {
      throw std::out_of_range("string_view::substr out of range");
    }
    return string_view(data_ + pos, std::min(len, size_ - pos));
  }

  int compare(string_view v) const noexcept {
    int cmp = traits_type::compare(data_, v.data_, std::min(size_, v.size_));
    if (cmp) {
      return cmp;
    }
    return static_cast<int>(size_) - static_cast<int>(v.size_);
  }
  int compare(size_type pos1, size_type count1, string_view v) const { return substr(pos1, count1).compare(v); }
  int compare(size_type pos1, size_type count1, string_view v, size_type pos2, size_type count2) const { return substr(pos1, count1).compare(v.substr(pos2, count2)); }
  int compare(const char_type* s) const { return compare(string_view(s)); }
  int compare(size_type pos1, size_type count1, const char_type* s) const { return substr(pos1, count1).compare(string_view(s)); }
  int compare(size_type pos1, size_type count1, const char_type* s, size_type count2) const { return substr(pos1, count1).compare(string_view(s, count2)); }

  static size_type find(string_view v, size_type pos = 0) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find not implemented yet");
  }
  static size_type find(char_type ch, size_type pos = 0) { return find(string_view(&ch, 1), pos); }
  static size_type find(const char_type* s, size_type pos, size_type count) { return find(string_view(s, count), pos); }
  static size_type find(const char_type* s, size_type pos = 0) { return find(string_view(s), pos); }

  static size_type rfind(string_view v, size_type pos = npos) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::rfind not implemented yet");
  }
  static size_type rfind(char_type c, size_type pos = npos) { return rfind(string_view(&c, 1), pos); }
  static size_type rfind(const char_type* s, size_type pos, size_type count) { return rfind(string_view(s, count), pos); }
  static size_type rfind(const char_type* s, size_type pos = npos) { return rfind(string_view(s), pos); }

  static size_type find_first_of(string_view v, size_type pos = 0) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_first_of not implemented yet");
  }
  static size_type find_first_of(char_type c, size_type pos = 0) { return find_first_of(string_view(&c, 1), pos); }
  static size_type find_first_of(const char_type* s, size_type pos, size_type count) { return find_first_of(string_view(s, count), pos); }
  static size_type find_first_of(const char_type* s, size_type pos = 0) { return find_first_of(string_view(s), pos); }

  static size_type find_last_of(string_view v, size_type pos = npos) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_last_of not implemented yet");
  }
  static size_type find_last_of(char_type c, size_type pos = npos) { return find_last_of(string_view(&c, 1), pos); }
  static size_type find_last_of(const char_type* s, size_type pos, size_type count) { return find_last_of(string_view(s, count), pos); }
  static size_type find_last_of(const char_type* s, size_type pos = npos) { return find_last_of(string_view(s), pos); }

  static size_type find_first_not_of(string_view v, size_type pos = 0) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_first_not_of not implemented yet");
  }
  static size_type find_first_not_of(char_type c, size_type pos = 0) { return find_first_not_of(string_view(&c, 1), pos); }
  static size_type find_first_not_of(const char_type* s, size_type pos, size_type count) { return find_first_not_of(string_view(s, count), pos); }
  static size_type find_first_not_of(const char_type* s, size_type pos = 0) { return find_first_not_of(string_view(s), pos); }

  static size_type find_last_not_of(string_view v, size_type pos = npos) {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_last_not_of not implemented yet");
  }
  static size_type find_last_not_of(char_type c, size_type pos = npos) { return find_last_not_of(string_view(&c, 1), pos); }
  static size_type find_last_not_of(const char_type* s, size_type pos, size_type count) { return find_last_not_of(string_view(s, count), pos); }
  static size_type find_last_not_of(const char_type* s, size_type pos = npos) { return find_last_not_of(string_view(s), pos); }
};

A0_STATIC_INLINE
std::ostream& operator<<(std::ostream& os, const string_view& v) {
  return os.write(v.data(), v.size());
}

A0_STATIC_INLINE bool operator==(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) == 0;
}
A0_STATIC_INLINE bool operator!=(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) != 0;
}
A0_STATIC_INLINE bool operator<(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) < 0;
}
A0_STATIC_INLINE bool operator>(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) > 0;
}
A0_STATIC_INLINE bool operator<=(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) <= 0;
}
A0_STATIC_INLINE bool operator>=(const string_view& lhs, const string_view& rhs) noexcept {
  return lhs.compare(rhs) >= 0;
}

#if A0_CPP_17

#define A0_STRING_VIEW_CMP(T)                                                                           \
  A0_STATIC_INLINE bool operator==(T lhs, string_view rhs) noexcept { return string_view(lhs) == rhs; } \
  A0_STATIC_INLINE bool operator==(string_view lhs, T rhs) noexcept { return lhs == string_view(rhs); } \
  A0_STATIC_INLINE bool operator!=(T lhs, string_view rhs) noexcept { return string_view(lhs) != rhs; } \
  A0_STATIC_INLINE bool operator!=(string_view lhs, T rhs) noexcept { return lhs != string_view(rhs); } \
  A0_STATIC_INLINE bool operator<(T lhs, string_view rhs) noexcept { return string_view(lhs) < rhs; }   \
  A0_STATIC_INLINE bool operator<(string_view lhs, T rhs) noexcept { return lhs < string_view(rhs); }   \
  A0_STATIC_INLINE bool operator>(T lhs, string_view rhs) noexcept { return string_view(lhs) > rhs; }   \
  A0_STATIC_INLINE bool operator>(string_view lhs, T rhs) noexcept { return lhs > string_view(rhs); }   \
  A0_STATIC_INLINE bool operator<=(T lhs, string_view rhs) noexcept { return string_view(lhs) <= rhs; } \
  A0_STATIC_INLINE bool operator<=(string_view lhs, T rhs) noexcept { return lhs <= string_view(rhs); } \
  A0_STATIC_INLINE bool operator>=(T lhs, string_view rhs) noexcept { return string_view(lhs) >= rhs; } \
  A0_STATIC_INLINE bool operator>=(string_view lhs, T rhs) noexcept { return lhs >= string_view(rhs); }

A0_STRING_VIEW_CMP(std::string_view)
A0_STRING_VIEW_CMP(std::string)
A0_STRING_VIEW_CMP(const char*)

#undef A0_STRING_VIEW_CMP

#endif  // A0_CPP_17

}  // namespace a0

namespace std {

template <>
struct hash<a0::string_view> {
  size_t operator()(const a0::string_view& s) const noexcept {
    a0_buf_t buf{(uint8_t*)s.data(), s.size()};
    size_t result;
    a0_hash_eval(A0_HASH_STR, &buf, &result);
    return result;
  }
};

}  // namespace std
