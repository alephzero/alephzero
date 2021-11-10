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
  void reply(std::unordered_multimap<std::string, std::string> headers,
             string_view payload) {
    reply(Packet(std::move(headers), payload, ref));
  }
  void reply(string_view payload) {
    reply({}, payload);
  }
};

struct RpcServer : details::CppWrap<a0_rpc_server_t> {
  RpcServer() = default;
  RpcServer(
      RpcTopic,
      std::function<void(RpcRequest)> onrequest,
      std::function<void(string_view /* id */)> oncancel);
};

struct RpcClient : details::CppWrap<a0_rpc_client_t> {
  RpcClient() = default;
  explicit RpcClient(RpcTopic);

  void send(Packet, std::function<void(Packet)>);
  void send(std::unordered_multimap<std::string, std::string> headers,
            string_view payload,
            std::function<void(Packet)> callback) {
    send(Packet(std::move(headers), payload, ref), std::move(callback));
  }
  void send(string_view payload, std::function<void(Packet)> callback) {
    send({}, payload, std::move(callback));
  }

  Packet send_blocking(Packet);
  Packet send_blocking(std::unordered_multimap<std::string, std::string> headers,
                       string_view payload) {
    return send_blocking(Packet(std::move(headers), payload, ref));
  }
  Packet send_blocking(string_view payload) {
    return send_blocking({}, payload);
  }

  Packet send_blocking(Packet, TimeMono);
  Packet send_blocking(std::unordered_multimap<std::string, std::string> headers,
                       string_view payload,
                       TimeMono timeout) {
    return send_blocking(Packet(std::move(headers), payload, ref), timeout);
  }
  Packet send_blocking(string_view payload, TimeMono timeout) {
    return send_blocking({}, payload, timeout);
  }

  std::future<Packet> send(Packet);
  std::future<Packet> send(std::unordered_multimap<std::string, std::string> headers,
                           string_view payload) {
    return send(Packet(std::move(headers), payload, ref));
  }
  std::future<Packet> send(string_view payload) {
    return send({}, payload);
  }

  void cancel(string_view);
};

}  // namespace a0
