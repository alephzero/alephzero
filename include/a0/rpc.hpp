#pragma once

#include <a0/c_wrap.hpp>
#include <a0/deadman.hpp>
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
      RpcTopic topic,
      std::function<void(RpcRequest)> onrequest) : RpcServer(topic, std::move(onrequest), nullptr) {}
  RpcServer(
      RpcTopic topic,
      std::function<void(RpcRequest)> onrequest,
      std::function<void(string_view /* id */)> oncancel) : RpcServer(topic, Options{std::move(onrequest), std::move(oncancel), TIMEOUT_NEVER}) {}
};

struct RpcClient : details::CppWrap<a0_rpc_client_t> {
  enum class Action {
    IGNORE = A0_RPC_CLIENT_ACTION_IGNORE,
    RESEND = A0_RPC_CLIENT_ACTION_RESEND,
    CANCEL = A0_RPC_CLIENT_ACTION_CANCEL,
  };

  struct SendOptions;

  using Hook = std::function<Action(SendOptions*)>;

  static const Hook DO_IGNORE;
  static const Hook DO_RESEND;
  static const Hook DO_CANCEL;

  struct SendOptions {
    TimeMono timeout;
    Hook ontimeout;
    Hook ondisconnect;
    Hook onreconnect;
    std::function<void()> oncomplete;

    SendOptions();
  };

  RpcClient() = default;
  explicit RpcClient(RpcTopic);

  void send(Packet, std::function<void(Packet)>, SendOptions);
  void send(Packet pkt, TimeMono timeout, std::function<void(Packet)> onreply) {
    SendOptions opts;
    opts.timeout = timeout;
    send(pkt, std::move(onreply), std::move(opts));
  }
  void send(Packet pkt, std::function<void(Packet)> onreply) {
    send(pkt, std::move(onreply), SendOptions());
  }

  void send(string_view payload, std::function<void(Packet)> onreply, SendOptions opts) {
    send(Packet(payload, ref), std::move(onreply), std::move(opts));
  }
  void send(string_view payload, TimeMono timeout, std::function<void(Packet)> onreply) {
    send(Packet(payload, ref), timeout, std::move(onreply));
  }
  void send(string_view payload, std::function<void(Packet)> onreply) {
    send(Packet(payload, ref), std::move(onreply));
  }

  std::future<Packet> send(Packet, SendOptions);
  std::future<Packet> send(Packet pkt, TimeMono timeout) {
    SendOptions opts = {};
    opts.timeout = timeout;
    return send(pkt, std::move(opts));
  }
  std::future<Packet> send(Packet pkt) {
    return send(pkt, SendOptions());
  }

  std::future<Packet> send(string_view payload, SendOptions opts) {
    return send(Packet(payload, ref), std::move(opts));
  }
  std::future<Packet> send(string_view payload, TimeMono timeout) {
    return send(Packet(payload, ref), timeout);
  }
  std::future<Packet> send(string_view payload) {
    return send(Packet(payload, ref));
  }

  Packet send_blocking(Packet pkt, SendOptions opts) {
    return send(pkt, std::move(opts)).get();
  }
  Packet send_blocking(Packet pkt, TimeMono timeout) {
    SendOptions opts;
    opts.timeout = timeout;
    return send_blocking(pkt, std::move(opts));
  }
  Packet send_blocking(Packet pkt) {
    return send_blocking(pkt, SendOptions());
  }

  Packet send_blocking(string_view payload, SendOptions opts) {
    return send_blocking(Packet(payload, ref), std::move(opts));
  }
  Packet send_blocking(string_view payload, TimeMono timeout) {
    return send_blocking(Packet(payload, ref), timeout);
  }
  Packet send_blocking(string_view payload) {
    return send_blocking(Packet(payload, ref));
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
