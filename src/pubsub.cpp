#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/pubsub.h>
#include <a0/pubsub.hpp>
#include <a0/reader.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "c_wrap.hpp"
#include "file_opts.hpp"

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

SubscriberSync::SubscriberSync(PubSubTopic topic, ReaderInit init, ReaderIter iter) {
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
        return a0_subscriber_sync_init(c, c_topic, alloc, init, iter);
      },
      [](a0_subscriber_sync_t* c, SubscriberSyncImpl*) {
        a0_subscriber_sync_close(c);
      });
}

bool SubscriberSync::has_next() {
  CHECK_C;
  bool ret;
  check(a0_subscriber_sync_has_next(&*c, &ret));
  return ret;
}

Packet SubscriberSync::next() {
  CHECK_C;
  auto* impl = c_impl<SubscriberSyncImpl>(&c);

  a0_packet_t pkt;
  check(a0_subscriber_sync_next(&*c, &pkt));
  auto data = std::make_shared<std::vector<uint8_t>>();
  std::swap(*data, impl->data);
  return Packet(pkt, [data](a0_packet_t*) {});
}

namespace {

struct SubscriberImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;
};

}  // namespace

Subscriber::Subscriber(
    PubSubTopic topic,
    ReaderInit init,
    ReaderIter iter,
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

        return a0_subscriber_init(c, c_topic, alloc, init, iter, c_onpacket);
      },
      [](a0_subscriber_t* c, SubscriberImpl*) {
        a0_subscriber_close(c);
      });
}

Packet Subscriber::read_one(PubSubTopic topic, ReaderInit init, int flags) {
  auto cfo = c_fileopts(topic.file_opts);
  a0_pubsub_topic_t c_topic{topic.name.c_str(), &cfo};

  auto pkt_data = std::make_shared<std::vector<uint8_t>>();

  // The alloc will be reused for (potentially) multiple packets, especially if blocking.
  // We only need to keep the first one alive.
  struct data_t {
    std::vector<uint8_t>* pkt_data;
    std::vector<uint8_t> dummy;
  } data{pkt_data.get(), {}};

  a0_alloc_t alloc = {
      .user_data = &data,
      .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
        auto* data = (data_t*)user_data;
        if (data->pkt_data->empty()) {
          data->pkt_data->resize(size);
          *out = {data->pkt_data->data(), size};
        } else {
          data->dummy.resize(size);
          *out = {data->dummy.data(), size};
        }
        return A0_OK;
      },
      .dealloc = nullptr,
  };

  a0_packet_t c_pkt;
  check(a0_subscriber_read_one(c_topic, alloc, init, flags, &c_pkt));
  return Packet(c_pkt, [pkt_data](a0_packet_t*) {});
}

}  // namespace a0
