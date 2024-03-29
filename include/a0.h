#ifndef A0_H
#define A0_H

#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/cfg.h>
#include <a0/cmp.h>
#include <a0/deadman.h>
#include <a0/deadman_mtx.h>
#include <a0/discovery.h>
#include <a0/empty.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/event.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/latch.h>
#include <a0/log.h>
#include <a0/map.h>
#include <a0/middleware.h>
#include <a0/mtx.h>
#include <a0/packet.h>
#include <a0/pathglob.h>
#include <a0/prpc.h>
#include <a0/pubsub.h>
#include <a0/reader.h>
#include <a0/rpc.h>
#include <a0/thread_local.h>
#include <a0/tid.h>
#include <a0/time.h>
#include <a0/topic.h>
#include <a0/transport.h>
#include <a0/unused.h>
#include <a0/uuid.h>
#include <a0/writer.h>

#ifdef __cplusplus
#include <a0/arena.hpp>
#include <a0/buf.hpp>
#include <a0/c_wrap.hpp>
#include <a0/cfg.hpp>
#include <a0/deadman.hpp>
#include <a0/discovery.hpp>
#include <a0/env.hpp>
#include <a0/file.hpp>
#include <a0/log.hpp>
#include <a0/middleware.hpp>
#include <a0/packet.hpp>
#include <a0/pathglob.hpp>
#include <a0/prpc.hpp>
#include <a0/pubsub.hpp>
#include <a0/reader.hpp>
#include <a0/rpc.hpp>
#include <a0/string_view.hpp>
#include <a0/time.hpp>
#include <a0/topic.hpp>
#include <a0/transport.hpp>
#include <a0/writer.hpp>
#endif  // __cplusplus

#endif  // A0_H
