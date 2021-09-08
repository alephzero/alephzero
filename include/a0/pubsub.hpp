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

  PubSubTopic(const char* name)
      : PubSubTopic(std::string(name)) {}

  PubSubTopic(
      std::string name,
      File::Options file_opts = File::Options::DEFAULT)
      : name{std::move(name)}, file_opts{std::move(file_opts)} {}
};

struct Publisher : details::CppWrap<a0_publisher_t> {
  Publisher();
  Publisher(PubSubTopic);

  void pub(Packet);
  void pub(string_view sv) { pub(Packet(sv, ref)); }
};

struct SubscriberSync : details::CppWrap<a0_subscriber_sync_t> {
  SubscriberSync() = default;
  SubscriberSync(ReaderInit, ReaderIter);
  SubscriberSync(PubSubTopic, ReaderInit, ReaderIter);

  bool has_next();
  Packet next();
};

struct Subscriber : details::CppWrap<a0_subscriber_t> {
  Subscriber() = default;
  Subscriber(ReaderInit, ReaderIter, std::function<void(Packet)>);
  Subscriber(PubSubTopic, ReaderInit, ReaderIter, std::function<void(Packet)>);

  static Packet read_one(PubSubTopic, ReaderInit, int flags);
};

}  // namespace a0
