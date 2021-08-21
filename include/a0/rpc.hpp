#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/reader.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>

namespace a0 {

struct RpcTopic {
  std::string name;
  File::Options file_opts;

  RpcTopic(
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
    : name{std::move(name)}, file_opts{std::move(file_opts)} {}

  RpcTopic(const char* name) : RpcTopic(std::string(name)) {}
};

struct RpcServer;

struct RpcRequest : details::CppWrap<a0_rpc_request_t> {
  RpcServer server();
  Packet pkt();

  void reply(Packet);
  void reply(std::vector<std::pair<std::string, std::string>> headers,
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
  RpcClient(RpcTopic);

  void send(Packet, std::function<void(Packet)>);
  void send(std::vector<std::pair<std::string, std::string>> headers,
            string_view payload,
            std::function<void(Packet)> callback) {
    send(Packet(std::move(headers), payload, ref), std::move(callback));
  }
  void send(string_view payload, std::function<void(Packet)> callback) {
    send({}, payload, std::move(callback));
  }

  std::future<Packet> send(Packet);
  std::future<Packet> send(std::vector<std::pair<std::string, std::string>> headers,
                           string_view payload) {
    return send(Packet(std::move(headers), payload, ref));
  }
  std::future<Packet> send(string_view payload) {
    return send({}, payload);
  }

  void cancel(string_view);
};

}  // namespace a0
