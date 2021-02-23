#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/packet.h>
#include <a0/transport.h>
#include <a0/writer.h>

#include <stdlib.h>

#include "err_util.h"

#ifdef DEBUG
#include "assert.h"
#include "ref_cnt.h"
#include "unused.h"
#endif

A0_STATIC_INLINE
errno_t a0_writer_process(a0_writer_t*, a0_packet_t*);

A0_STATIC_INLINE
errno_t a0_write_process_chain(void* data, a0_packet_t* pkt) {
  a0_writer_t* next_writer = (a0_writer_t*)data;
  if (next_writer) {
    return a0_writer_process(next_writer, pkt);
  }
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_writer_process(a0_writer_t* w, a0_packet_t* pkt) {
  a0_writer_middleware_chain_t chain;
  chain.data = w->_next_writer;
  chain.chain_fn = a0_write_process_chain;

  return w->_action.process(w->_action.user_data, pkt, chain);
}

A0_STATIC_INLINE
errno_t a0_write_action_init(a0_arena_t arena, void** user_data) {
  a0_transport_t transport;
  A0_RETURN_ERR_ON_ERR(a0_transport_init(&transport, arena));

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(arena.buf.ptr, NULL), "");
#endif

  a0_transport_t* heap_transport = (a0_transport_t*)malloc(sizeof(a0_transport_t));
  *heap_transport = transport;
  *user_data = heap_transport;

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_write_action_close(void* user_data) {
  a0_transport_t* transport = (a0_transport_t*)user_data;

#ifdef DEBUG
  A0_ASSERT_OK(
      a0_ref_cnt_dec(transport->_arena.buf.ptr, NULL),
      "Writer closing. User bug detected. Dependent arena was closed prior to writer.");
#endif

  free(transport);

  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_write_action_process(void* user_data, a0_packet_t* pkt, a0_writer_middleware_chain_t chain) {
  a0_transport_t* transport = (a0_transport_t*)user_data;
  a0_locked_transport_t lk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(transport, &lk));

  a0_alloc_t alloc;
  A0_RETURN_ERR_ON_ERR(a0_transport_allocator(&lk, &alloc));
  A0_RETURN_ERR_ON_ERR(a0_packet_serialize(*pkt, alloc, NULL));

  A0_RETURN_ERR_ON_ERR(a0_transport_commit(lk));
  A0_RETURN_ERR_ON_ERR(a0_transport_unlock(lk));

  return a0_writer_middleware_chain(chain, pkt);
}

errno_t a0_writer_init(a0_writer_t* w, a0_arena_t arena) {
  A0_RETURN_ERR_ON_ERR(a0_write_action_init(arena, &w->_action.user_data));
  w->_action.close = a0_write_action_close;
  w->_action.process = a0_write_action_process;
  w->_next_writer = NULL;

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(w, NULL), "");
#endif

  return A0_OK;
}

errno_t a0_writer_close(a0_writer_t* w) {
#ifdef DEBUG
  A0_ASSERT(w, "Cannot close null writer.");

  if (w->_next_writer) {
    size_t next_writer_ref_cnt;
    A0_MAYBE_UNUSED(next_writer_ref_cnt);
    A0_ASSERT_OK(
        a0_ref_cnt_dec(w->_next_writer, &next_writer_ref_cnt),
        "Closing writer while still in use.");
    A0_ASSERT(
        next_writer_ref_cnt > 0,
        "Closing writer while still in use.");
  }

  size_t ref_cnt;
  A0_MAYBE_UNUSED(ref_cnt);
  A0_ASSERT_OK(
      a0_ref_cnt_dec(w, &ref_cnt),
      "Failed to decrement writer count.");
  A0_ASSERT(
      ref_cnt == 0,
      "Closing writer while still in use.");
#endif

  if (w->_action.close) {
    A0_RETURN_ERR_ON_ERR(w->_action.close(w->_action.user_data));
  }

  return A0_OK;
}

errno_t a0_writer_write(a0_writer_t* w, a0_packet_t pkt) {
  return a0_writer_process(w, &pkt);
}

errno_t a0_writer_wrap(a0_writer_t* in, a0_writer_middleware_t middleware, a0_writer_t* out) {
  out->_action = middleware;
  out->_next_writer = in;

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(out->_next_writer, NULL), "");
  A0_ASSERT_OK(a0_ref_cnt_inc(out, NULL), "");
#endif

  return A0_OK;
}

typedef struct a0_middleware_pair_s {
  a0_writer_middleware_t first;
  a0_writer_middleware_t second;
} a0_middleware_pair_t;

typedef struct a0_middleware_chain_s {
  a0_writer_middleware_t middleware;
  a0_writer_middleware_chain_t chain;
} a0_middleware_chain_t;

A0_STATIC_INLINE
errno_t a0_compose_init(a0_writer_middleware_t first, a0_writer_middleware_t second, void** user_data) {
  a0_middleware_pair_t* heap_middleware_pair = (a0_middleware_pair_t*)malloc(sizeof(a0_middleware_pair_t));
  heap_middleware_pair->first = first;
  heap_middleware_pair->second = second;
  *user_data = heap_middleware_pair;
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_compose_close(void* user_data) {
  a0_middleware_pair_t* pair = (a0_middleware_pair_t*)user_data;
  if (pair->first.close) {
    pair->first.close(pair->first.user_data);
  }
  if (pair->second.close) {
    pair->second.close(pair->second.user_data);
  }
  free(pair);
  return A0_OK;
}

A0_STATIC_INLINE
errno_t a0_compose_process_chain(void* data, a0_packet_t* pkt) {
  a0_middleware_chain_t* chain_2_data = (a0_middleware_chain_t*)data;
  return chain_2_data->middleware.process(
      chain_2_data->middleware.user_data, pkt, chain_2_data->chain);
}

A0_STATIC_INLINE
errno_t a0_compose_process(void* user_data, a0_packet_t* pkt, a0_writer_middleware_chain_t chain_2) {
  a0_middleware_pair_t* middleware_pair = (a0_middleware_pair_t*)user_data;

  a0_middleware_chain_t chain_2_data;
  chain_2_data.middleware = middleware_pair->second;
  chain_2_data.chain = chain_2;

  a0_writer_middleware_chain_t chain_1 = {
      .data = &chain_2_data,
      .chain_fn = a0_compose_process_chain,
  };

  return middleware_pair->first.process(middleware_pair->first.user_data, pkt, chain_1);
}

errno_t a0_writer_middleware_compose(a0_writer_middleware_t first, a0_writer_middleware_t second, a0_writer_middleware_t* out) {
  A0_RETURN_ERR_ON_ERR(a0_compose_init(first, second, &out->user_data));
  out->close = a0_compose_close;
  out->process = a0_compose_process;
  return A0_OK;
}
