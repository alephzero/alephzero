#pragma once

#include <a0/c_wrap.hpp>
#include <a0/deadman.hpp>
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
  File::Options file_opts{File::Options::DEFAULT};

  PrpcTopic() = default;

  PrpcTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : PrpcTopic(std::string(name)) {}

  PrpcTopic(  // NOLINT(google-explicit-constructor)
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{file_opts} {}
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
  struct Options {
    std::function<void(PrpcConnection)> onconnect;
    std::function<void(string_view /* id */)> oncancel;

    TimeMono exclusive_ownership_timeout;
  };

  PrpcServer() = default;
  PrpcServer(PrpcTopic, Options);

  // Backwards compatible constructors.
  PrpcServer(
      PrpcTopic topic,
      std::function<void(PrpcConnection)> onconnect) : PrpcServer(topic, std::move(onconnect), nullptr) {}
  PrpcServer(
      PrpcTopic topic,
      std::function<void(PrpcConnection)> onconnect,
      std::function<void(string_view /* id */)> oncancel) : PrpcServer(topic, Options{std::move(onconnect), std::move(oncancel), TIMEOUT_NEVER}) {}
};

struct PrpcClient : details::CppWrap<a0_prpc_client_t> {
  PrpcClient() = default;
  explicit PrpcClient(PrpcTopic);

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

  Deadman server_deadman();
  uint64_t server_wait_up();
  uint64_t server_wait_up(TimeMono);
  void server_wait_down(uint64_t);
  void server_wait_down(uint64_t, TimeMono);
  Deadman::State server_state();
};

}  // namespace a0
