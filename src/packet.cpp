#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/uuid.h>

#include <errno.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "c_wrap.hpp"
#include "err_macro.h"

namespace a0 {
namespace {

struct PacketImpl {
  std::unordered_multimap<std::string, std::string> cpp_hdrs;
  std::vector<a0_packet_header_t> c_hdrs;
};

std::shared_ptr<a0_packet_t> make_cpp_packet(
    string_view id,
    std::unordered_multimap<std::string, std::string> hdrs,
    string_view payload_view,
    std::function<void(a0_packet_t*)> deleter) {
  std::shared_ptr<a0_packet_t> c;
  set_c_impl<PacketImpl>(
      &c,
      [&](a0_packet_t* c, PacketImpl* impl) {
        // Handle id.

        if (id.empty()) {
          // Create a new ID.
          A0_RETURN_ERR_ON_ERR(a0_packet_init(&*c));
        } else if (id.size() == A0_UUID_SIZE - 1) {
          memcpy(c->id, id.data(), A0_UUID_SIZE - 1);
          c->id[A0_UUID_SIZE - 1] = '\0';
        } else {
          // TODO(lshamis): Handle corrupt ids.
          return EINVAL;
        }

        // Handle headers.

        impl->cpp_hdrs = std::move(hdrs);

        for (const auto& elem : impl->cpp_hdrs) {
          impl->c_hdrs.push_back(a0_packet_header_t{
              .key = elem.first.c_str(),
              .val = elem.second.c_str(),
          });
        }

        c->headers_block = {
            .headers = impl->c_hdrs.data(),
            .size = impl->c_hdrs.size(),
            .next_block = nullptr,
        };

        // Handle payload.

        c->payload = as_buf(payload_view);

        return A0_OK;
      },
      [deleter](a0_packet_t* c, PacketImpl*) {
        if (deleter) {
          deleter(c);
        }
      });
  return c;
}

}  // namespace

Packet::Packet()
    : Packet(std::string{}) {}

Packet::Packet(std::string payload)
    : Packet({}, std::move(payload)) {}

Packet::Packet(std::unordered_multimap<std::string, std::string> headers,
               std::string payload) {
  auto owned_payload = std::make_shared<std::string>(std::move(payload));
  c = make_cpp_packet(
      std::string{},
      std::move(headers),
      *owned_payload,
      [owned_payload](a0_packet_t*) {});
}

Packet::Packet(string_view payload, tag_ref_t ref)
    : Packet({}, std::move(payload), ref) {}

Packet::Packet(std::unordered_multimap<std::string, std::string> headers,
               string_view payload, tag_ref_t) {
  c = make_cpp_packet(
      std::string{},
      std::move(headers),
      payload,
      nullptr);
}

Packet::Packet(a0_packet_t pkt, std::function<void(a0_packet_t*)> deleter) {
  std::unordered_multimap<std::string, std::string> hdrs;

  a0_packet_header_iterator_t iter;
  a0_packet_header_iterator_init(&iter, &pkt);
  a0_packet_header_t hdr;
  while (!a0_packet_header_iterator_next(&iter, &hdr)) {
    hdrs.insert({hdr.key, hdr.val});
  }

  c = make_cpp_packet(
      pkt.id,
      std::move(hdrs),
      string_view((char*)pkt.payload.ptr, pkt.payload.size),
      deleter);
}

string_view Packet::id() const {
  CHECK_C;
  return c->id;
}

const std::unordered_multimap<std::string, std::string>& Packet::headers() const {
  CHECK_C;
  auto* impl = c_impl<PacketImpl>(&c);
  return impl->cpp_hdrs;
}

string_view Packet::payload() const {
  CHECK_C;
  return string_view((char*)c->payload.ptr, c->payload.size);
}

string_view FlatPacket::id() const {
  CHECK_C;
  a0_uuid_t* uuid;
  check(a0_flat_packet_id(*c, &uuid));
  return string_view(*uuid, sizeof(a0_uuid_t));
}

string_view FlatPacket::payload() const {
  CHECK_C;
  a0_buf_t buf;
  check(a0_flat_packet_payload(*c, &buf));
  return string_view((const char*)buf.ptr, buf.size);
}

size_t FlatPacket::num_headers() const {
  CHECK_C;
  a0_packet_stats_t stats;
  check(a0_flat_packet_stats(*c, &stats));
  return stats.num_hdrs;
}

std::pair<string_view, string_view> FlatPacket::header(size_t idx) const {
  CHECK_C;
  a0_packet_header_t hdr;
  check(a0_flat_packet_header(*c, idx, &hdr));
  return {hdr.key, hdr.val};
}

}  // namespace a0
