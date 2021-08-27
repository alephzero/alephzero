#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/packet.hpp>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/reader.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>

namespace a0 {

struct PrpcTopic {
  std::string name;
  File::Options file_opts;

  PrpcTopic(
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
    : name{std::move(name)}, file_opts{std::move(file_opts)} {}

  PrpcTopic(const char* name) : PrpcTopic(std::string(name)) {}
};

struct PrpcServer;

struct PrpcConnection : details::CppWrap<a0_prpc_connection_t> {
  PrpcServer server();
  Packet pkt();

  void send(Packet, bool done);
  void send(std::unordered_multimap<std::string, std::string> headers,
            string_view payload,
            bool done) {
    send(Packet(std::move(headers), payload, ref), done);
  }
  void send(string_view payload, bool done) {
    send({}, payload, done);
  }
};

struct PrpcServer : details::CppWrap<a0_prpc_server_t> {
  PrpcServer() = default;
  PrpcServer(
      PrpcTopic,
      std::function<void(PrpcConnection)> onconnection,
      std::function<void(string_view /* id */)> oncancel);
};

struct PrpcClient : details::CppWrap<a0_prpc_client_t> {
  PrpcClient() = default;
  PrpcClient(PrpcTopic);

  void connect(Packet, std::function<void(Packet, bool /* done */)>);
  void connect(std::unordered_multimap<std::string, std::string> headers,
               string_view payload,
               std::function<void(Packet, bool /* done */)> onprogress) {
    connect(Packet(std::move(headers), payload, ref), std::move(onprogress));
  }
  void connect(string_view payload, std::function<void(Packet, bool /* done */)> onprogress) {
    connect({}, payload, std::move(onprogress));
  }

  void cancel(string_view);
};

}  // namespace a0
