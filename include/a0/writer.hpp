#pragma once

#include <a0/arena.hpp>
#include <a0/c_wrap.hpp>
#include <a0/middleware.hpp>
#include <a0/packet.hpp>
#include <a0/writer.h>

#include <cstdint>

namespace a0 {

struct Writer : details::CppWrap<a0_writer_t> {
  Writer() = default;
  Writer(Arena);

  void write(Packet);

  void push(Middleware);
  Writer wrap(Middleware);
};

}  // namespace a0
