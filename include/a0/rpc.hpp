#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/reader.hpp>
#include <a0/rpc.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>

namespace a0 {

struct RpcTopic {
  std::string name;
  File::Options file_opts{File::Options::DEFAULT};

  RpcTopic() = default;

  RpcTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : RpcTopic(std::string(name)) {}

  RpcTopic(  // NOLINT(google-explicit-constructor)
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{file_opts} {}
};

struct RpcServer;

struct RpcRequest : details::CppWrap<a0_rpc_request_t> {
  RpcServer server();
  Packet pkt();

  void reply(Packet);
  void reply(string_view payload) {
    reply(Packet(payload, ref));
  }
};

struct RpcServer : details::CppWrap<a0_rpc_server_t> {
  struct Options {
    std::function<void(RpcRequest)> onrequest;
    std::function<void(string_view /* id */)> oncancel;

    TimeMono exclusive_ownership_timeout;
  };

  RpcServer() = default;
  RpcServer(RpcTopic, Options);

  // Backwards compatible constructors.
  RpcServer(
      RpcTopic,
      std::function<void(RpcRequest)> onrequest);
  RpcServer(
      RpcTopic,
      std::function<void(RpcRequest)> onrequest,
      std::function<void(string_view /* id */)> oncancel);
};

struct RpcClient : details::CppWrap<a0_rpc_client_t> {
  RpcClient() = default;
  explicit RpcClient(RpcTopic);

  void send(Packet, TimeMono, std::function<void(Packet)>, std::function<void()>);
  void send(Packet pkt, std::function<void(Packet)> onreply) {
    send(pkt, a0::TIMEOUT_NEVER, std::move(onreply), nullptr);
  }

  void send(string_view payload, TimeMono timeout, std::function<void(Packet)> onreply, std::function<void()> ontimeout) {
    return send(Packet(payload, ref), timeout, std::move(onreply), std::move(ontimeout));
  }
  void send(string_view payload, std::function<void(Packet)> onreply) {
    send(payload, a0::TIMEOUT_NEVER, std::move(onreply), nullptr);
  }

  std::future<Packet> send(Packet, TimeMono);
  std::future<Packet> send(Packet pkt) {
    return send(pkt, a0::TIMEOUT_NEVER);
  }

  std::future<Packet> send(string_view payload, TimeMono timeout) {
    return send(Packet(payload, ref), timeout);
  }
  std::future<Packet> send(string_view payload) {
    return send(payload, a0::TIMEOUT_NEVER);
  }

  Packet send_blocking(Packet pkt, TimeMono timeout) {
    return send(pkt, timeout).get();
  }
  Packet send_blocking(Packet pkt) {
    return send_blocking(pkt, a0::TIMEOUT_NEVER);
  }

  Packet send_blocking(string_view payload, TimeMono timeout) {
    return send_blocking(Packet(payload, ref), timeout);
  }
  Packet send_blocking(string_view payload) {
    return send_blocking(payload, a0::TIMEOUT_NEVER);
  }

  void cancel(string_view);
};

}  // namespace a0
