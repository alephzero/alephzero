#include <a0/alloc.h>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/log.h>
#include <a0/log.hpp>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/reader.hpp>

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

Logger::Logger(LogTopic topic) {
  set_c(
      &c,
      [&](a0_logger_t* c) {
        auto cfo = c_fileopts(topic.file_opts);
        a0_log_topic_t c_topic{topic.name.c_str(), &cfo};
        return a0_logger_init(c, c_topic);
      },
      a0_logger_close);
}

void Logger::log(LogLevel lvl, Packet pkt) {
  CHECK_C;
  check(a0_logger_log(&*c, (a0_log_level_t)lvl, *pkt.c));
}

void Logger::crit(Packet pkt) {
  CHECK_C;
  check(a0_logger_crit(&*c, *pkt.c));
}

void Logger::err(Packet pkt) {
  CHECK_C;
  check(a0_logger_err(&*c, *pkt.c));
}

void Logger::warn(Packet pkt) {
  CHECK_C;
  check(a0_logger_warn(&*c, *pkt.c));
}

void Logger::info(Packet pkt) {
  CHECK_C;
  check(a0_logger_info(&*c, *pkt.c));
}

void Logger::dbg(Packet pkt) {
  CHECK_C;
  check(a0_logger_dbg(&*c, *pkt.c));
}

namespace {

struct LogListenerImpl {
  std::vector<uint8_t> data;
  std::function<void(Packet)> onpacket;
};

}  // namespace

LogListener::LogListener(
    LogTopic topic,
    LogLevel lvl,
    Reader::Qos qos,
    std::function<void(Packet)> onpacket) {
  set_c_impl<LogListenerImpl>(
      &c,
      [&](a0_log_listener_t* c, LogListenerImpl* impl) {
        impl->onpacket = std::move(onpacket);

        auto cfo = c_fileopts(topic.file_opts);
        a0_log_topic_t c_topic{topic.name.c_str(), &cfo};

        a0_alloc_t alloc = {
            .user_data = impl,
            .alloc = [](void* user_data, size_t size, a0_buf_t* out) {
              auto* impl = (LogListenerImpl*)user_data;
              impl->data.resize(size);
              *out = {impl->data.data(), size};
              return A0_OK;
            },
            .dealloc = nullptr,
        };

        a0_packet_callback_t c_onpacket = {
            .user_data = impl,
            .fn = [](void* user_data, a0_packet_t pkt) {
              auto* impl = (LogListenerImpl*)user_data;
              auto data = std::make_shared<std::vector<uint8_t>>();
              std::swap(*data, impl->data);
              impl->onpacket(Packet(pkt, [data](a0_packet_t*) {}));
            }};

        return a0_log_listener_init(c, c_topic, alloc, (a0_log_level_t)lvl, c_qos(qos), c_onpacket);
      },
      [](a0_log_listener_t* c, LogListenerImpl*) {
        a0_log_listener_close(c);
      });
}

}  // namespace a0
