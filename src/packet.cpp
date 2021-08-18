#include <a0/packet.hpp>

#include "c_wrap.hpp"

namespace a0 {

struct PacketImpl {
  bool is_view = false;
  std::string cpp_payload;
  std::vector<std::pair<std::string, std::string>> cpp_hdrs;
  std::vector<a0_packet_header_t> c_hdrs;

  void operator()(a0_packet_t* self) {
    delete self;
  }
};

namespace {

std::shared_ptr<a0_packet_t> make_cpp_packet(
    a0::string_view id,
    std::vector<std::pair<std::string, std::string>> hdrs,
    std::string payload,
    a0::string_view payload_view,
    bool is_view) {
  // Create basic object.

  std::shared_ptr<a0_packet_t> c(new a0_packet_t, PacketImpl{});
  memset(&*c, 0, sizeof(a0_packet_t));
  auto* impl = std::get_deleter<PacketImpl>(c);

  // Handle id.

  if (id.empty()) {
    // Create a new ID.
    check(a0_packet_init(&*c));
  } else if (id.size() == A0_UUID_SIZE - 1) {
    memcpy(c->id, id.data(), A0_UUID_SIZE - 1);
    c->id[A0_UUID_SIZE - 1] = '\0';
  } else {
    // TODO(lshamis): Handle corrupt ids.
    throw;
  }

  // Handle headers.

  impl->cpp_hdrs = std::move(hdrs);

  for (size_t i = 0; i < impl->cpp_hdrs.size(); i++) {
    impl->c_hdrs.push_back(a0_packet_header_t{
        .key = impl->cpp_hdrs[i].first.c_str(),
        .val = impl->cpp_hdrs[i].second.c_str(),
    });
  }

  c->headers_block = {
      .headers = impl->c_hdrs.data(),
      .size = impl->c_hdrs.size(),
      .next_block = nullptr,
  };

  // Handle payload.

  if (is_view) {
    c->payload = as_buf(payload_view);
  } else {
    impl->cpp_payload = std::move(payload);
    c->payload = as_buf(impl->cpp_payload);
  }

  return c;
}

}  // namespace

Packet::Packet()
    : Packet(std::string{}) {}

Packet::Packet(std::string payload)
    : Packet({}, std::move(payload)) {}

Packet::Packet(std::vector<std::pair<std::string, std::string>> headers,
               std::string payload) {
  c = make_cpp_packet("", std::move(headers), std::move(payload), "", false);
}

Packet::Packet(a0::string_view payload, tag_ref_t ref)
    : Packet({}, std::move(payload), ref) {}

Packet::Packet(std::vector<std::pair<std::string, std::string>> headers,
               a0::string_view payload, tag_ref_t) {
  c = make_cpp_packet("", std::move(headers), "", payload, true);
}

Packet::Packet(a0_packet_t pkt) {
  std::vector<std::pair<std::string, std::string>> hdrs;

  a0_packet_header_iterator_t iter;
  a0_packet_header_iterator_init(&iter, &pkt.headers_block);
  a0_packet_header_t hdr;
  while (!a0_packet_header_iterator_next(&iter, &hdr)) {
    hdrs.push_back({hdr.key, hdr.val});
  }

  a0::string_view payload_view((char*)pkt.payload.ptr, pkt.payload.size);

  c = make_cpp_packet(pkt.id, std::move(hdrs), "", payload_view, true);
}

a0::string_view Packet::id() const {
  CHECK_C;
  return c->id;
}

const std::vector<std::pair<std::string, std::string>>& Packet::headers() const {
  CHECK_C;
  auto* impl = std::get_deleter<PacketImpl>(c);
  return impl->cpp_hdrs;
}

a0::string_view Packet::payload() const {
  CHECK_C;
  return a0::string_view((char*)c->payload.ptr, c->payload.size);
}

}  // namespace a0
