#pragma once

#include <memory>

namespace a0 {
namespace details {

template <typename CType>
struct CppWrap {
  using c_type = CType;

  std::shared_ptr<CType> c;
  uint32_t magic_number{0xA0A0A0A0};

  CppWrap() = default;
  ~CppWrap() {
    magic_number = 0xDEADBEEF;
  }
};

}  // namespace details
}  // namespace a0
