#pragma once

#if (__cplusplus >= 201703L)

#include <string_view>

namespace a0 {
using string_view = std::string_view;
}  // namespace a0

#else

#include <a0/inline.h>
#include <a0/unused.h>

#include <cstring>
#include <algorithm>
#include <string>
#include <ostream>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <iterator>
#include <ios>

namespace a0 {

class string_view {
  const char* ptr_;
  size_t size_;

 public:
  using char_type   = char;
  using traits_type = std::char_traits<char>;
  using size_type   = size_t;

  using value_type      = char;
  using reference       = char&;
  using const_reference = const char&;
  using pointer         = char*;
  using const_pointer   = const char*;

  using iterator       = const char*;
  using const_iterator = const char*;
  using reverse_iterator = std::reverse_iterator<const char*>;
  using const_reverse_iterator = std::reverse_iterator<const char*>;

  static const size_t npos = size_t(-1);

  explicit operator std::string() const { return std::string(ptr_, size_); }

  string_view() noexcept : ptr_{nullptr}, size_{0} {}
  string_view(const string_view& other) noexcept = default;
  string_view(const char* s, size_t count) noexcept : ptr_{s}, size_{count} {}
  string_view(const char* s) noexcept : string_view(s, strlen(s)) {}
  string_view(const std::string& str) noexcept : ptr_{str.c_str()}, size_{str.size()} {}
  
  string_view& operator=(const string_view& view) = default;

  const_iterator begin() const noexcept { return ptr_; }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator end() const noexcept { return ptr_ + size_; }
  const_iterator cend() const noexcept { return end(); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  const_reference at(size_type pos) const {
    if (pos >= size_) {
      throw std::out_of_range("string_view::at out of range");
    }
    return ptr_[pos];
  }
  const_reference operator[](size_type pos) const noexcept { return ptr_[pos]; }
  const_reference front() const noexcept { return *ptr_; }
  const_reference back() const noexcept { return ptr_[size_ - 1]; }
  const_pointer data() const noexcept { return ptr_; }

  size_type size() const noexcept { return size_; }
  size_type length() const noexcept { return size_; }
  size_type max_size() const noexcept { return npos - 1; }
  bool empty() const noexcept { return !size_; }

  void remove_prefix(size_type n) noexcept {
    ptr_ += n;
    size_ -= n;
  }
  void remove_suffix(size_type n) noexcept { size_ -= n; }
  void swap(string_view& v) noexcept {
    std::swap(ptr_, v.ptr_);
    std::swap(size_, v.size_);
  }

  size_type copy(char_type* dest, size_type count, size_type pos = 0) const {
    if(pos >= size_) {
      throw std::out_of_range("string_view::copy out of range");
    }
    size_t rcount = std::min(size_ - pos, count + 1);
    memcpy(dest, ptr_ + pos, rcount);
    return rcount;
  }
  string_view substr(size_type pos = 0, size_type len = npos) const {
    if(pos >= size_) {
      throw std::out_of_range("string_view::substr out of range");
    }
    return string_view(ptr_ + pos, std::min(len, size_ - pos));
  }

  int compare(string_view v) const noexcept {
    int cmp = traits_type::compare(ptr_, v.ptr_, std::min(size_, v.size_));
    if (cmp) {
      return cmp;
    }
    return size_ - v.size_;
  }
  int compare(size_type pos1, size_type count1, string_view v) const { return substr(pos1, count1).compare(v); }
  int compare(size_type pos1, size_type count1, string_view v, size_type pos2, size_type count2) const { return substr(pos1, count1).compare(v.substr(pos2, count2)); }
  int compare(const char_type* s) const { return compare(string_view(s)); }
  int compare(size_type pos1, size_type count1, const char_type* s) const { return substr(pos1, count1).compare(string_view(s)); }
  int compare(size_type pos1, size_type count1, const char_type* s, size_type count2) const { return substr(pos1, count1).compare(string_view(s, count2)); }

  size_type find(string_view v, size_type pos = 0) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find not implemented yet");
  }
  size_type find(char_type ch, size_type pos = 0) const { return find(string_view(&ch, 1), pos); }
  size_type find(const char_type* s, size_type pos, size_type count) const { return find(string_view(s, count), pos); }
  size_type find(const char_type* s, size_type pos = 0) const { return find(string_view(s), pos); }

  size_type rfind(string_view v, size_type pos = npos) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::rfind not implemented yet");
  }
  size_type rfind(char_type c, size_type pos = npos) const { return rfind(string_view(&c, 1), pos); }
  size_type rfind(const char_type* s, size_type pos, size_type count) const { return rfind(string_view(s, count), pos); }
  size_type rfind(const char_type* s, size_type pos = npos) const { return rfind(string_view(s), pos); }

  size_type find_first_of(string_view v, size_type pos = 0) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_first_of not implemented yet");
  }
  size_type find_first_of(char_type c, size_type pos = 0) const { return find_first_of(string_view(&c, 1), pos); }
  size_type find_first_of(const char_type* s, size_type pos, size_type count) const { return find_first_of(string_view(s, count), pos); }
  size_type find_first_of(const char_type* s, size_type pos = 0) const { return find_first_of(string_view(s), pos); }

  size_type find_last_of(string_view v, size_type pos = npos) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_last_of not implemented yet");
  }
  size_type find_last_of(char_type c, size_type pos = npos) const { return find_last_of(string_view(&c, 1), pos); }
  size_type find_last_of(const char_type* s, size_type pos, size_type count) const { return find_last_of(string_view(s, count), pos); }
  size_type find_last_of(const char_type* s, size_type pos = npos) const { return find_last_of(string_view(s), pos); }

  size_type find_first_not_of(string_view v, size_type pos = 0) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_first_not_of not implemented yet");
  }
  size_type find_first_not_of(char_type c, size_type pos = 0) const { return find_first_not_of(string_view(&c, 1), pos); }
  size_type find_first_not_of(const char_type* s, size_type pos, size_type count) const { return find_first_not_of(string_view(s, count), pos); }
  size_type find_first_not_of(const char_type* s, size_type pos = 0) const { return find_first_not_of(string_view(s), pos); }

  size_type find_last_not_of(string_view v, size_type pos = npos) const {
    A0_MAYBE_UNUSED(v);
    A0_MAYBE_UNUSED(pos);
    throw std::logic_error("a0::string_view::find_last_not_of not implemented yet");
  }
  size_type find_last_not_of(char_type c, size_type pos = npos) const { return find_last_not_of(string_view(&c, 1), pos);}
  size_type find_last_not_of(const char_type* s, size_type pos, size_type count) const { return find_last_not_of(string_view(s, count), pos);}
  size_type find_last_not_of(const char_type* s, size_type pos = npos) const { return find_last_not_of(string_view(s), pos);}
};

A0_STATIC_INLINE
std::ostream& operator<<(std::ostream& os, const string_view& v) {
  return os.write(v.data(), v.size());
}

A0_STATIC_INLINE bool operator==(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) == 0; }
A0_STATIC_INLINE bool operator!=(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) != 0; }
A0_STATIC_INLINE bool operator<(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) < 0; }
A0_STATIC_INLINE bool operator>(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) > 0; }
A0_STATIC_INLINE bool operator<=(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) <= 0; }
A0_STATIC_INLINE bool operator>=(const string_view& lhs, const string_view& rhs) noexcept { return lhs.compare(rhs) >= 0; }

} // namespace a0

#endif
