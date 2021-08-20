#include <a0/arena.hpp>
#include <a0/middleware.h>
#include <a0/middleware.hpp>
#include <a0/packet.hpp>
#include <a0/pubsub.hpp>
#include <a0/writer.h>
#include <a0/writer.hpp>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

Publisher::Publisher(PubSubTopic topic) {
  a0_file_options_t c_file_opts{
      .create_options = {
          .size = topic.file_opts.create_options.size,
          .mode = topic.file_opts.create_options.mode,
          .dir_mode = topic.file_opts.create_options.dir_mode,
      },
      .open_options = {
          .arena_mode = topic.file_opts.open_options.arena_mode,
      },
  };
  set_c(
      &c,
      [&](a0_publisher_t* c) {
        a0_pubsub_topic_t c_topic = {topic.name.c_str(), &c_file_opts};
        return a0_publisher_init(c, c_topic);
      },
      a0_publisher_close);
}

void Publisher::pub(Packet pkt) {
  CHECK_C;
  check(a0_publisher_pub(&*c, *pkt.c));
}

}  // namespace a0
