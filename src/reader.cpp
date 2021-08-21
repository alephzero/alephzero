#include <a0/alloc.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/reader.h>
#include <a0/reader.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <cstddef>
#include <functional>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "c_wrap.hpp"

namespace a0 {

ReaderSyncZeroCopy::ReaderSyncZeroCopy(Arena arena, ReaderInit init, ReaderIter iter) {
  set_c(
      &c,
      [&](a0_reader_sync_zc_t* c) {
        return a0_reader_sync_zc_init(c, *arena.c, init, iter);
      },
      [arena](a0_reader_sync_zc_t* c) {
        a0_reader_sync_zc_close(c);
      });
}

bool ReaderSyncZeroCopy::has_next() {
  CHECK_C;
  bool ret;
  check(a0_reader_sync_zc_has_next(&*c, &ret));
  return ret;
}

void ReaderSyncZeroCopy::next(std::function<void(LockedTransport, FlatPacket)> fn) {
  CHECK_C;

  a0_zero_copy_callback_t cb = {
      .user_data = &fn,
      .fn = [](void* user_data, a0_locked_transport_t tlk, a0_flat_packet_t fpkt) {
        auto* fn = (std::function<void(LockedTransport, FlatPacket)>*)user_data;
        auto cpp_tlk = make_cpp<LockedTransport>(
            [&](a0_locked_transport_t* c_cpp_tlk) {
              *c_cpp_tlk = tlk;
              return A0_OK;
            });
        auto cpp_fpkt = make_cpp<FlatPacket>(
            [&](a0_flat_packet_t* c_cpp_fpkt) {
              *c_cpp_fpkt = fpkt;
              return A0_OK;
            });
        (*fn)(cpp_tlk, cpp_fpkt);
      },
  };

  check(a0_reader_sync_zc_next(&*c, cb));
}

namespace {

struct ReaderSyncImpl {
  Arena arena;
  std::vector<uint8_t> data;
};

}  // namespace

ReaderSync::ReaderSync(Arena arena, ReaderInit init, ReaderIter iter) {
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
        return a0_reader_sync_init(c, *arena.c, alloc, init, iter);
      },
      [](a0_reader_sync_t* c, ReaderSyncImpl*) {
        a0_reader_sync_close(c);
      });
}

bool ReaderSync::has_next() {
  CHECK_C;
  bool ret;
  check(a0_reader_sync_has_next(&*c, &ret));
  return ret;
}

Packet ReaderSync::next() {
  CHECK_C;
  auto* impl = c_impl<ReaderSyncImpl>(&c);

  a0_packet_t pkt;
  check(a0_reader_sync_next(&*c, &pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

namespace {

struct ReaderZeroCopyImpl {
  std::function<void(LockedTransport, FlatPacket)> cb;
};

}  // namespace

ReaderZeroCopy::ReaderZeroCopy(
    Arena arena,
    ReaderInit init,
    ReaderIter iter,
    std::function<void(LockedTransport, FlatPacket)> cb) {
  set_c_impl<ReaderZeroCopyImpl>(
      &c,
      [&](a0_reader_zc_t* c, ReaderZeroCopyImpl* impl) {
        impl->cb = std::move(cb);

        a0_zero_copy_callback_t c_cb = {
            .user_data = impl,
            .fn = [](void* user_data, a0_locked_transport_t tlk, a0_flat_packet_t fpkt) {
                auto* impl = (ReaderZeroCopyImpl*)user_data;
                impl->cb(cpp_wrap<LockedTransport>(tlk), cpp_wrap<FlatPacket>(fpkt));
            },
        };

        return a0_reader_zc_init(c, *arena.c, init, iter, c_cb);
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
    ReaderInit init,
    ReaderIter iter,
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
            }
        };

        return a0_reader_init(c, *arena.c, alloc, init, iter, c_cb);
      },
      [](a0_reader_t* c, ReaderImpl*) {
        a0_reader_close(c);
      });
}

Packet Reader::read_one(Arena arena, ReaderInit init, int flags) {
  auto data = std::make_shared<std::vector<uint8_t>>();
  a0_alloc_t alloc = {
      .user_data = data.get(),
      .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
        auto* data = (std::vector<uint8_t>*)user_data;
        data->resize(size);
        *out = {data->data(), size};
        return A0_OK;
      },
      .dealloc = nullptr,
  };

  a0_packet_t c_pkt;
  check(a0_reader_read_one(*arena.c, alloc, init, flags, &c_pkt));
  return Packet(c_pkt, [data](a0_packet_t*) {});
}

}  // namespace a0
