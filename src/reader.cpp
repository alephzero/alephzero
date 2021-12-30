#include <a0/alloc.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/reader.h>
#include <a0/reader.hpp>
#include <a0/time.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "c_opts.hpp"
#include "c_wrap.hpp"

namespace a0 {

Reader::Options Reader::Options::DEFAULT = {
    (Reader::Init)A0_READER_OPTIONS_DEFAULT.init,
    (Reader::Iter)A0_READER_OPTIONS_DEFAULT.iter,
};

ReaderSyncZeroCopy::ReaderSyncZeroCopy(Arena arena, Reader::Options opts) {
  set_c(
      &c,
      [&](a0_reader_sync_zc_t* c) {
        return a0_reader_sync_zc_init(c, *arena.c, c_readeropts(opts));
      },
      [arena](a0_reader_sync_zc_t* c) {
        a0_reader_sync_zc_close(c);
      });
}

bool ReaderSyncZeroCopy::can_read() {
  CHECK_C;
  bool ret;
  check(a0_reader_sync_zc_can_read(&*c, &ret));
  return ret;
}

A0_STATIC_INLINE
a0_zero_copy_callback_t ReadZeroCopy_CallbackWrapper(std::function<void(TransportLocked, FlatPacket)>* fn) {
  return {
      .user_data = fn,
      .fn = [](void* user_data, a0_transport_locked_t tlk, a0_flat_packet_t fpkt) {
        auto* fn = (std::function<void(TransportLocked, FlatPacket)>*)user_data;
        (*fn)(
            cpp_wrap<TransportLocked>(tlk),
            cpp_wrap<FlatPacket>(fpkt));
      },
  };
}

void ReaderSyncZeroCopy::read(std::function<void(TransportLocked, FlatPacket)> fn) {
  CHECK_C;
  check(a0_reader_sync_zc_read(&*c, ReadZeroCopy_CallbackWrapper(&fn)));
}

void ReaderSyncZeroCopy::read_blocking(std::function<void(TransportLocked, FlatPacket)> fn) {
  CHECK_C;
  check(a0_reader_sync_zc_read_blocking(&*c, ReadZeroCopy_CallbackWrapper(&fn)));
}

void ReaderSyncZeroCopy::read_blocking(TimeMono timeout, std::function<void(TransportLocked, FlatPacket)> fn) {
  CHECK_C;
  check(a0_reader_sync_zc_read_blocking_timeout(&*c, *timeout.c, ReadZeroCopy_CallbackWrapper(&fn)));
}

namespace {

struct ReaderSyncImpl {
  Arena arena;
  std::vector<uint8_t> data;
};

}  // namespace

ReaderSync::ReaderSync(Arena arena, Reader::Options opts) {
  set_c_impl<ReaderSyncImpl>(
      &c,
      [&](a0_reader_sync_t* c, ReaderSyncImpl* impl) {
        impl->arena = arena;

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (ReaderSyncImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };
        return a0_reader_sync_init(c, *arena.c, alloc, c_readeropts(opts));
      },
      [](a0_reader_sync_t* c, ReaderSyncImpl*) {
        a0_reader_sync_close(c);
      });
}

bool ReaderSync::can_read() {
  CHECK_C;
  bool ret;
  check(a0_reader_sync_can_read(&*c, &ret));
  return ret;
}

Packet ReaderSync::read() {
  CHECK_C;
  auto* impl = c_impl<ReaderSyncImpl>(&c);

  a0_packet_t pkt;
  check(a0_reader_sync_read(&*c, &pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

Packet ReaderSync::read_blocking() {
  CHECK_C;
  auto* impl = c_impl<ReaderSyncImpl>(&c);

  a0_packet_t pkt;
  check(a0_reader_sync_read_blocking(&*c, &pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

Packet ReaderSync::read_blocking(TimeMono timeout) {
  CHECK_C;
  auto* impl = c_impl<ReaderSyncImpl>(&c);

  a0_packet_t pkt;
  check(a0_reader_sync_read_blocking_timeout(&*c, *timeout.c, &pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

namespace {

struct ReaderZeroCopyImpl {
  std::function<void(TransportLocked, FlatPacket)> cb;
};

}  // namespace

ReaderZeroCopy::ReaderZeroCopy(
    Arena arena,
    Reader::Options opts,
    std::function<void(TransportLocked, FlatPacket)> cb) {
  set_c_impl<ReaderZeroCopyImpl>(
      &c,
      [&](a0_reader_zc_t* c, ReaderZeroCopyImpl* impl) {
        impl->cb = std::move(cb);

        a0_zero_copy_callback_t c_cb = {
            .user_data = impl,
            .fn = [](void* user_data, a0_transport_locked_t tlk, a0_flat_packet_t fpkt) {
              auto* impl = (ReaderZeroCopyImpl*)user_data;
              impl->cb(cpp_wrap<TransportLocked>(tlk), cpp_wrap<FlatPacket>(fpkt));
            },
        };

        return a0_reader_zc_init(c, *arena.c, c_readeropts(opts), c_cb);
      },
      [arena](a0_reader_zc_t* c, ReaderZeroCopyImpl*) {
        a0_reader_zc_close(c);
      });
}

namespace {

struct ReaderImpl {
  Arena arena;
  std::vector<uint8_t> data;
  std::function<void(Packet)> cb;
};

}  // namespace

Reader::Reader(
    Arena arena,
    Reader::Options opts,
    std::function<void(Packet)> cb) {
  set_c_impl<ReaderImpl>(
      &c,
      [&](a0_reader_t* c, ReaderImpl* impl) {
        impl->arena = arena;
        impl->cb = cb;

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (ReaderImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_cb = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (ReaderImpl*)user_data;
              auto data = std::make_shared<std::vector<uint8_t>>();
              std::swap(*data, impl->data);
              impl->cb(Packet(pkt, [data](a0_packet_t*) {}));
            }};

        return a0_reader_init(c, *arena.c, alloc, c_readeropts(opts), c_cb);
      },
      [](a0_reader_t* c, ReaderImpl*) {
        a0_reader_close(c);
      });
}

void read_random_access(Arena arena, size_t off, std::function<void(TransportLocked, FlatPacket)> fn) {
  check(a0_read_random_access(*arena.c, off, ReadZeroCopy_CallbackWrapper(&fn)));
}

}  // namespace a0
