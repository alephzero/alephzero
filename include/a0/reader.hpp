#pragma once

#include <a0/arena.hpp>
#include <a0/c_wrap.hpp>
#include <a0/packet.hpp>
#include <a0/reader.h>
#include <a0/transport.hpp>

#include <functional>

namespace a0 {

struct Reader : details::CppWrap<a0_reader_t> {
  enum struct Init {
    OLDEST = A0_INIT_OLDEST,
    MOST_RECENT = A0_INIT_MOST_RECENT,
    AWAIT_NEW = A0_INIT_AWAIT_NEW,
  };

  enum struct Iter {
    NEXT = A0_ITER_NEXT,
    NEWEST = A0_ITER_NEWEST,
  };

  struct Options {
    Init init;
    Iter iter;
    static Options DEFAULT;

    Options()
        : Options{DEFAULT} {}
    explicit Options(Init init_)
        : Options() { init = init_; }
    explicit Options(Iter iter_)
        : Options() { iter = iter_; }
    Options(Init init_, Iter iter_)
        : Options() {
      init = init_;
      iter = iter_;
    }
  };

  Reader() = default;
  Reader(Arena, Options, std::function<void(Packet)>);

  Reader(Arena arena, std::function<void(Packet)> fn)
      : Reader(arena, Options(), fn) {}
  Reader(Arena arena, Init init, std::function<void(Packet)> fn)
      : Reader(arena, Options(init), fn) {}
  Reader(Arena arena, Iter iter, std::function<void(Packet)> fn)
      : Reader(arena, Options(iter), fn) {}
  Reader(Arena arena, Init init, Iter iter, std::function<void(Packet)> fn)
      : Reader(arena, Options(init, iter), fn) {}
};

static const Reader::Init& INIT_OLDEST = Reader::Init::OLDEST;
static const Reader::Init& INIT_MOST_RECENT = Reader::Init::MOST_RECENT;
static const Reader::Init& INIT_AWAIT_NEW = Reader::Init::AWAIT_NEW;
static const Reader::Iter& ITER_NEXT = Reader::Iter::NEXT;
static const Reader::Iter& ITER_NEWEST = Reader::Iter::NEWEST;

struct ReaderSyncZeroCopy : details::CppWrap<a0_reader_sync_zc_t> {
  ReaderSyncZeroCopy() = default;
  ReaderSyncZeroCopy(Arena, Reader::Options);

  explicit ReaderSyncZeroCopy(Arena arena)
      : ReaderSyncZeroCopy(arena, Reader::Options()) {}
  ReaderSyncZeroCopy(Arena arena, Reader::Init init)
      : ReaderSyncZeroCopy(arena, Reader::Options(init)) {}
  ReaderSyncZeroCopy(Arena arena, Reader::Iter iter)
      : ReaderSyncZeroCopy(arena, Reader::Options(iter)) {}
  ReaderSyncZeroCopy(Arena arena, Reader::Init init, Reader::Iter iter)
      : ReaderSyncZeroCopy(arena, Reader::Options(init, iter)) {}

  bool can_read();
  void read(std::function<void(TransportLocked, FlatPacket)>);
  void read_blocking(std::function<void(TransportLocked, FlatPacket)>);
  void read_blocking(TimeMono, std::function<void(TransportLocked, FlatPacket)>);
};

struct ReaderSync : details::CppWrap<a0_reader_sync_t> {
  ReaderSync() = default;
  ReaderSync(Arena, Reader::Options);

  explicit ReaderSync(Arena arena)
      : ReaderSync(arena, Reader::Options()) {}
  ReaderSync(Arena arena, Reader::Init init)
      : ReaderSync(arena, Reader::Options(init)) {}
  ReaderSync(Arena arena, Reader::Iter iter)
      : ReaderSync(arena, Reader::Options(iter)) {}
  ReaderSync(Arena arena, Reader::Init init, Reader::Iter iter)
      : ReaderSync(arena, Reader::Options(init, iter)) {}

  bool can_read();
  Packet read();
  Packet read_blocking();
  Packet read_blocking(TimeMono);
};

struct ReaderZeroCopy : details::CppWrap<a0_reader_zc_t> {
  ReaderZeroCopy() = default;
  ReaderZeroCopy(Arena, Reader::Options, std::function<void(TransportLocked, FlatPacket)>);

  ReaderZeroCopy(Arena arena, std::function<void(TransportLocked, FlatPacket)> fn)
      : ReaderZeroCopy(arena, Reader::Options(), fn) {}
  ReaderZeroCopy(Arena arena, Reader::Init init, std::function<void(TransportLocked, FlatPacket)> fn)
      : ReaderZeroCopy(arena, Reader::Options(init), fn) {}
  ReaderZeroCopy(Arena arena, Reader::Iter iter, std::function<void(TransportLocked, FlatPacket)> fn)
      : ReaderZeroCopy(arena, Reader::Options(iter), fn) {}
  ReaderZeroCopy(Arena arena, Reader::Init init, Reader::Iter iter, std::function<void(TransportLocked, FlatPacket)> fn)
      : ReaderZeroCopy(arena, Reader::Options(init, iter), fn) {}
};

void read_random_access(Arena, size_t off, std::function<void(TransportLocked, FlatPacket)>);

}  // namespace a0
