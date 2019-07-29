#ifndef A0_INTERNAL_STREAM_DEBUG_H
#define A0_INTERNAL_STREAM_DEBUG_H

#include <a0/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

void a0_stream_debugstr(a0_locked_stream_t*, a0_buf_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_INTERNAL_STREAM_DEBUG_H
