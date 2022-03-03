#include <a0/middleware.h>
#include <a0/middleware.hpp>

#include "c_wrap.hpp"

namespace a0 {

Middleware add_time_mono_header() {
  return cpp_wrap<Middleware>(a0_add_time_mono_header());
}

Middleware add_time_wall_header() {
  return cpp_wrap<Middleware>(a0_add_time_wall_header());
}

Middleware add_writer_id_header() {
  return cpp_wrap<Middleware>(a0_add_writer_id_header());
}

Middleware add_writer_seq_header() {
  return cpp_wrap<Middleware>(a0_add_writer_seq_header());
}

Middleware add_transport_seq_header() {
  return cpp_wrap<Middleware>(a0_add_transport_seq_header());
}

Middleware add_standard_headers() {
  return cpp_wrap<Middleware>(a0_add_standard_headers());
}

Middleware write_if_empty(bool* written) {
  return cpp_wrap<Middleware>(a0_write_if_empty(written));
}

Middleware json_mergepatch() {
  return cpp_wrap<Middleware>(a0_json_mergepatch());
}

}  // namespace a0
