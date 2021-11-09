#pragma once

#include <a0/arena.hpp>
#include <a0/c_wrap.hpp>
#include <a0/packet.hpp>
#include <a0/reader.h>
#include <a0/transport.hpp>

#include <functional>

namespace a0 {

using ReaderInit = a0_reader_init_t;
using ReaderIter = a0_reader_iter_t;

struct ReaderSyncZeroCopy : details::CppWrap<a0_reader_sync_zc_t> {
  ReaderSyncZeroCopy() = default;
  ReaderSyncZeroCopy(Arena, ReaderInit, ReaderIter);

  bool has_next();
  void next(std::function<void(TransportLocked, FlatPacket)>);
  void next_blocking(std::function<void(TransportLocked, FlatPacket)>);
};

struct ReaderSync : details::CppWrap<a0_reader_sync_t> {
  ReaderSync() = default;
  ReaderSync(Arena, ReaderInit, ReaderIter);

  bool has_next();
  Packet next();
  Packet next_blocking();
};

struct ReaderZeroCopy : details::CppWrap<a0_reader_zc_t> {
  ReaderZeroCopy() = default;
  ReaderZeroCopy(Arena, ReaderInit, ReaderIter, std::function<void(TransportLocked, FlatPacket)>);
};

struct Reader : details::CppWrap<a0_reader_t> {
  Reader() = default;
  Reader(Arena, ReaderInit, ReaderIter, std::function<void(Packet)>);
};

}  // namespace a0
