#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/pubsub.hpp>
#include <a0/reader.hpp>
#include <a0/time.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "c_opts.hpp"
#include "c_wrap.hpp"

namespace a0 {

Publisher::Publisher(PubSubTopic topic) {
  set_c(
      &c,
      [&](a0_publisher_t* c) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_pubsub_topic_t c_topic{topic.name.c_str(), &cfo};
        return a0_publisher_init(c, c_topic);
      },
      a0_publisher_close);
}

void Publisher::pub(Packet pkt) {
  CHECK_C;
  check(a0_publisher_pub(&*c, *pkt.c));
}

namespace {

struct SubscriberSyncImpl {
  std::vector<uint8_t> data;
};

}  // namespace

SubscriberSync::SubscriberSync(PubSubTopic topic, Reader::Options opts) {
  set_c_impl<SubscriberSyncImpl>(
      &c,
      [&](a0_subscriber_sync_t* c, SubscriberSyncImpl* impl) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_pubsub_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (SubscriberSyncImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };
        return a0_subscriber_sync_init(c, c_topic, alloc, c_readeropts(opts));
      },
      [](a0_subscriber_sync_t* c, SubscriberSyncImpl*) {
        a0_subscriber_sync_close(c);
      });
}

bool SubscriberSync::can_read() {
  CHECK_C;
  bool ret;
  check(a0_subscriber_sync_can_read(&*c, &ret));
  return ret;
}

A0_STATIC_INLINE
Packet SubscriberSync_read(SubscriberSyncImpl* impl, std::function<a0_err_t(a0_packet_t*)> fn) {
  a0_packet_t pkt;
  check(fn(&pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

Packet SubscriberSync::read() {
  CHECK_C;
  return SubscriberSync_read(c_impl<SubscriberSyncImpl>(&c), [&](a0_packet_t* pkt) {
    return a0_subscriber_sync_read(&*c, pkt);
  });
}

Packet SubscriberSync::read_blocking() {
  CHECK_C;
  return SubscriberSync_read(c_impl<SubscriberSyncImpl>(&c), [&](a0_packet_t* pkt) {
    return a0_subscriber_sync_read_blocking(&*c, pkt);
  });
}

Packet SubscriberSync::read_blocking(TimeMono timeout) {
  CHECK_C;
  return SubscriberSync_read(c_impl<SubscriberSyncImpl>(&c), [&](a0_packet_t* pkt) {
    return a0_subscriber_sync_read_blocking_timeout(&*c, *timeout.c, pkt);
  });
}

namespace {

struct SubscriberImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;
};

}  // namespace

Subscriber::Subscriber(
    PubSubTopic topic,
    Reader::Options opts,
    std::function<void(Packet)> onpacket) {
  set_c_impl<SubscriberImpl>(
      &c,
      [&](a0_subscriber_t* c, SubscriberImpl* impl) {
        impl->onpacket = std::move(onpacket);

        auto cfo = c_fileopts(topic.file_opts);
        a0_pubsub_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (SubscriberImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_onpacket = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (SubscriberImpl*)user_data;
              auto data = std::make_shared<std::vector<uint8_t>>();
              std::swap(*data, impl->data);
              impl->onpacket(Packet(pkt, [data](a0_packet_t*) {}));
            }};

        return a0_subscriber_init(c, c_topic, alloc, c_readeropts(opts), c_onpacket);
      },
      [](a0_subscriber_t* c, SubscriberImpl*) {
        a0_subscriber_close(c);
      });
}

}  // namespace a0
