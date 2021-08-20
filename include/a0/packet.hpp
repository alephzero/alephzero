#pragma once

#include <a0/c_wrap.hpp>
#include <a0/packet.h>
#include <a0/string_view.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace a0 {

static const struct tag_ref_t {} ref{};

/// Packet is immutable.
struct Packet : details::CppWrap<a0_packet_t> {
  /// Creates a new packet with no headers and an empty payload.
  Packet();
  /// Creates a new packet with no headers and the given payload.
  explicit Packet(std::string payload);
  /// ...
  Packet(string_view payload, tag_ref_t);
  /// Creates a new packet with the given headers and the given payload.
  Packet(std::vector<std::pair<std::string, std::string>> headers,
         std::string payload);
  /// ...
  Packet(std::vector<std::pair<std::string, std::string>> headers,
         string_view payload, tag_ref_t);

  Packet(a0_packet_t, std::function<void(a0_packet_t*)> deleter);

  /// Packet unique identifier.
  string_view id() const;
  /// Packet headers.
  const std::vector<std::pair<std::string, std::string>>& headers() const;
  /// Packet payload.
  string_view payload() const;
};

/// FlatPacket is immutable.
struct FlatPacket : details::CppWrap<a0_flat_packet_t> {
  string_view id() const;
  string_view payload() const;
  size_t num_headers() const;
  std::pair<string_view, string_view> header(size_t idx) const;
};

}  // namespace a0
