#pragma once

#include <a0/c_wrap.hpp>
#include <a0/middleware.h>

#include <cstdint>

namespace a0 {

struct Middleware : details::CppWrap<a0_middleware_t> {};

Middleware add_time_mono_header();
Middleware add_time_wall_header();
Middleware add_writer_id_header();
Middleware add_writer_seq_header();
Middleware add_transport_seq_header();
Middleware add_standard_headers();

}  // namespace a0
