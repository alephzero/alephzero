#pragma once

#include <a0/c_wrap.hpp>
#include <a0/file.hpp>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/reader.hpp>
#include <a0/writer.hpp>

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
  Writer writer();
};

struct SubscriberSync : details::CppWrap<a0_subscriber_sync_t> {
  SubscriberSync() = default;
  SubscriberSync(PubSubTopic, Reader::Options);

  explicit SubscriberSync(PubSubTopic topic)
      : SubscriberSync(topic, Reader::Options()) {}
  SubscriberSync(PubSubTopic topic, Reader::Init init)
      : SubscriberSync(topic, Reader::Options(init)) {}
  SubscriberSync(PubSubTopic topic, Reader::Iter iter)
      : SubscriberSync(topic, Reader::Options(iter)) {}
  SubscriberSync(PubSubTopic topic, Reader::Init init, Reader::Iter iter)
      : SubscriberSync(topic, Reader::Options(init, iter)) {}

  // Deprecated.
  SubscriberSync(PubSubTopic topic, a0_reader_init_t init, a0_reader_iter_t iter)
      : SubscriberSync(topic, Reader::Init(init), Reader::Iter(iter)) {}

  bool can_read();
  Packet read();
  Packet read_blocking();
  Packet read_blocking(TimeMono);
};

struct Subscriber : details::CppWrap<a0_subscriber_t> {
  Subscriber() = default;
  Subscriber(PubSubTopic, Reader::Options, std::function<void(Packet)>);

  Subscriber(PubSubTopic topic, std::function<void(Packet)> fn)
      : Subscriber(topic, Reader::Options(), fn) {}
  Subscriber(PubSubTopic topic, Reader::Init init, std::function<void(Packet)> fn)
      : Subscriber(topic, Reader::Options(init), fn) {}
  Subscriber(PubSubTopic topic, Reader::Iter iter, std::function<void(Packet)> fn)
      : Subscriber(topic, Reader::Options(iter), fn) {}
  Subscriber(PubSubTopic topic, Reader::Init init, Reader::Iter iter, std::function<void(Packet)> fn)
      : Subscriber(topic, Reader::Options(init, iter), fn) {}

  // Deprecated.
  Subscriber(PubSubTopic topic, a0_reader_init_t init, a0_reader_iter_t iter, std::function<void(Packet)> fn)
      : Subscriber(topic, Reader::Init(init), Reader::Iter(iter), fn) {}
};

}  // namespace a0
