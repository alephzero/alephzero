#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/reader.hpp>

#include <cstddef>
#include <cstdint>

namespace a0 {

struct PubSubTopic {
  std::string name;
  File::Options file_opts{File::Options::DEFAULT};

  PubSubTopic() = default;

  PubSubTopic(const char* name)  // NOLINT(google-explicit-constructor)
      : PubSubTopic(std::string(name)) {}

  PubSubTopic(  // NOLINT(google-explicit-constructor)
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{file_opts} {}
};

struct Publisher : details::CppWrap<a0_publisher_t> {
  Publisher() = default;
  explicit Publisher(PubSubTopic);

  void pub(Packet);
  void pub(std::unordered_multimap<std::string, std::string> headers,
           string_view payload) {
    pub(Packet(std::move(headers), payload, ref));
  }
  void pub(string_view payload) {
    pub({}, payload);
  }
};

struct SubscriberSync : details::CppWrap<a0_subscriber_sync_t> {
  SubscriberSync() = default;
  SubscriberSync(PubSubTopic, ReaderInit, ReaderIter);

  bool can_read();
  Packet read();
  Packet read_blocking();
  Packet read_blocking(TimeMono);
};

struct Subscriber : details::CppWrap<a0_subscriber_t> {
  Subscriber() = default;
  Subscriber(PubSubTopic, ReaderInit, ReaderIter, std::function<void(Packet)>);
};

}  // namespace a0
