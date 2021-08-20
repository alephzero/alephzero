#include <a0/err.h>
#include <a0/middleware.h>
#include <a0/middleware.hpp>

#include "c_wrap.hpp"

namespace a0 {

Middleware add_time_mono_header() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_time_mono_header();
        return A0_OK;
      });
}

Middleware add_time_wall_header() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_time_wall_header();
        return A0_OK;
      });
}

Middleware add_writer_id_header() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_writer_id_header();
        return A0_OK;
      });
}

Middleware add_writer_seq_header() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_writer_seq_header();
        return A0_OK;
      });
}

Middleware add_transport_seq_header() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_transport_seq_header();
        return A0_OK;
      });
}

Middleware add_standard_headers() {
  return make_cpp<Middleware>(
      [&](a0_middleware_t* c) {
        *c = a0_add_standard_headers();
        return A0_OK;
      });
}

}  // namespace a0
