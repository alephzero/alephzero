#ifndef A0_INTERNAL_TRANSPORT_DEBUG_H
#define A0_INTERNAL_TRANSPORT_DEBUG_H

#include <a0/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

void a0_transport_debugstr(a0_locked_transport_t, a0_buf_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_INTERNAL_TRANSPORT_DEBUG_H
