#include <a0/arena.hpp>
#include <a0/middleware.h>
#include <a0/middleware.hpp>
#include <a0/packet.hpp>
#include <a0/writer.h>
#include <a0/writer.hpp>

#include <memory>

#include "c_wrap.hpp"

namespace a0 {

Writer::Writer(Arena arena) {
  set_c(
      &c,
      [&](a0_writer_t* c) {
        return a0_writer_init(c, *arena.c);
      },
      [arena](a0_writer_t* c) {
        a0_writer_close(c);
      });
}

void Writer::write(Packet pkt) {
  CHECK_C;
  check(a0_writer_write(&*c, *pkt.c));
}

void Writer::push(Middleware m) {
  CHECK_C;
  check(a0_writer_push(&*c, *m.c));
}

Writer Writer::wrap(Middleware m) {
  CHECK_C;
  auto save = c;
  return make_cpp<Writer>(
      [&](a0_writer_t* c) {
        return a0_writer_wrap(&*save, *m.c, c);
      },
      [save](a0_writer_t* c) {
        a0_writer_close(c);
      });
}

}  // namespace a0
