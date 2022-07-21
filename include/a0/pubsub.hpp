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
  void pub(string_view payload) {
    pub(Packet(payload, ref));
  }
  Writer writer();
};

struct SubscriberSyncZeroCopy : details::CppWrap<a0_subscriber_sync_zc_t> {
  SubscriberSyncZeroCopy() = default;
  SubscriberSyncZeroCopy(PubSubTopic, Reader::Options);

  explicit SubscriberSyncZeroCopy(PubSubTopic topic)
      : SubscriberSyncZeroCopy(topic, Reader::Options()) {}
  SubscriberSyncZeroCopy(PubSubTopic topic, Reader::Init init)
      : SubscriberSyncZeroCopy(topic, Reader::Options(init)) {}
  SubscriberSyncZeroCopy(PubSubTopic topic, Reader::Iter iter)
      : SubscriberSyncZeroCopy(topic, Reader::Options(iter)) {}
  SubscriberSyncZeroCopy(PubSubTopic topic, Reader::Init init, Reader::Iter iter)
      : SubscriberSyncZeroCopy(topic, Reader::Options(init, iter)) {}

  bool can_read();
  void read(std::function<void(TransportLocked, FlatPacket)>);
  void read_blocking(std::function<void(TransportLocked, FlatPacket)>);
  void read_blocking(TimeMono, std::function<void(TransportLocked, FlatPacket)>);
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

struct SubscriberZeroCopy : details::CppWrap<a0_subscriber_zc_t> {
  SubscriberZeroCopy() = default;
  SubscriberZeroCopy(PubSubTopic, Reader::Options, std::function<void(TransportLocked, FlatPacket)>);

  SubscriberZeroCopy(PubSubTopic topic, std::function<void(TransportLocked, FlatPacket)> fn)
      : SubscriberZeroCopy(topic, Reader::Options(), fn) {}
  SubscriberZeroCopy(PubSubTopic topic, Reader::Init init, std::function<void(TransportLocked, FlatPacket)> fn)
      : SubscriberZeroCopy(topic, Reader::Options(init), fn) {}
  SubscriberZeroCopy(PubSubTopic topic, Reader::Iter iter, std::function<void(TransportLocked, FlatPacket)> fn)
      : SubscriberZeroCopy(topic, Reader::Options(iter), fn) {}
  SubscriberZeroCopy(PubSubTopic topic, Reader::Init init, Reader::Iter iter, std::function<void(TransportLocked, FlatPacket)> fn)
      : SubscriberZeroCopy(topic, Reader::Options(init, iter), fn) {}
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
