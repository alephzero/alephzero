#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/middleware.h>
#include <a0/packet.h>
#include <a0/transport.h>
#include <a0/unused.h>
#include <a0/writer.h>

#include <stdlib.h>

#include "err_macro.h"

#ifdef DEBUG
#include <a0/unused.h>

#include "assert.h"
#include "ref_cnt.h"
#endif

A0_STATIC_INLINE_RECURSIVE
a0_err_t a0_writer_write_impl(a0_middleware_chain_node_t node, a0_packet_t* pkt) {
  a0_middleware_t action = node._curr->_action;
  a0_middleware_chain_t chain = {
      ._node = {
          ._curr = node._curr->_next,
          ._head = node._head,
          ._tlk = node._tlk,
      },
      ._chain_fn = a0_writer_write_impl,
  };

  if (!node._tlk.transport) {
    if (!action.process) {
      return a0_middleware_chain(chain, pkt);
    }
    return action.process(action.user_data, pkt, chain);
  } else {
    if (!action.process_locked) {
      return a0_middleware_chain(chain, pkt);
    }
    return action.process_locked(action.user_data, node._tlk, pkt, chain);
  }
}

A0_STATIC_INLINE
a0_err_t a0_write_action_init(a0_arena_t arena, void** user_data) {
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
a0_err_t a0_write_action_close(void* user_data) {
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
a0_err_t a0_write_action_process(void* user_data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  a0_transport_t* transport = (a0_transport_t*)user_data;
  a0_transport_locked_t tlk;
  A0_RETURN_ERR_ON_ERR(a0_transport_lock(transport, &tlk));

  a0_middleware_chain_node_t next_node = {
      ._curr = chain._node._head,
      ._head = chain._node._head,
      ._tlk = tlk,
  };

  return a0_writer_write_impl(next_node, pkt);
}

A0_STATIC_INLINE
a0_err_t a0_write_action_process_locked(void* user_data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(user_data);
  A0_MAYBE_UNUSED(chain);

  a0_alloc_t alloc;
  a0_transport_allocator(&tlk, &alloc);
  a0_packet_serialize(*pkt, alloc, NULL);

  a0_transport_commit(tlk);
  a0_transport_unlock(tlk);

  return A0_OK;
}

a0_err_t a0_writer_init(a0_writer_t* w, a0_arena_t arena) {
  A0_RETURN_ERR_ON_ERR(a0_write_action_init(arena, &w->_action.user_data));
  w->_action.close = a0_write_action_close;
  w->_action.process = a0_write_action_process;
  w->_action.process_locked = a0_write_action_process_locked;
  w->_next = NULL;

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(w, NULL), "");
#endif

  return A0_OK;
}

a0_err_t a0_writer_close(a0_writer_t* w) {
#ifdef DEBUG
  A0_ASSERT(w, "Cannot close null writer.");

  if (w->_next) {
    size_t next_writer_ref_cnt;
    A0_ASSERT_OK(
        a0_ref_cnt_dec(w->_next, &next_writer_ref_cnt),
        "Closing writer while still in use.");
    A0_ASSERT(
        next_writer_ref_cnt > 0,
        "Closing writer while still in use.");
  }

  size_t ref_cnt;
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

a0_err_t a0_writer_write(a0_writer_t* w, a0_packet_t pkt) {
  a0_middleware_chain_node_t node = {
      ._curr = w,
      ._head = w,
      ._tlk = A0_EMPTY,
  };
  return a0_writer_write_impl(node, &pkt);
}

a0_err_t a0_writer_wrap(a0_writer_t* in, a0_middleware_t middleware, a0_writer_t* out) {
  out->_action = middleware;
  out->_next = in;

#ifdef DEBUG
  A0_ASSERT_OK(a0_ref_cnt_inc(out->_next, NULL), "");
  A0_ASSERT_OK(a0_ref_cnt_inc(out, NULL), "");
#endif

  return A0_OK;
}

a0_err_t a0_writer_push(a0_writer_t* w, a0_middleware_t middleware) {
  A0_RETURN_ERR_ON_ERR(a0_middleware_compose(middleware, w->_action, &w->_action));
  return A0_OK;
}

typedef struct a0_compose_pair_s {
  a0_middleware_t first;
  a0_middleware_t second;
} a0_compose_pair_t;

A0_STATIC_INLINE
a0_err_t a0_compose_init(a0_middleware_t first, a0_middleware_t second, void** user_data) {
  a0_compose_pair_t* heap_middleware_pair = (a0_compose_pair_t*)malloc(sizeof(a0_compose_pair_t));
  heap_middleware_pair->first = first;
  heap_middleware_pair->second = second;
  *user_data = heap_middleware_pair;
  return A0_OK;
}

A0_STATIC_INLINE
a0_err_t a0_compose_close(void* user_data) {
  a0_compose_pair_t* pair = (a0_compose_pair_t*)user_data;
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
a0_err_t a0_compose_process(void* user_data, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  a0_compose_pair_t* middleware_pair = (a0_compose_pair_t*)user_data;

  a0_writer_t second_writer = {
      ._action = middleware_pair->second,
      ._next = chain._node._curr,
  };

  a0_writer_t first_writer = {
      ._action = middleware_pair->first,
      ._next = &second_writer,
  };

  a0_middleware_chain_node_t node = chain._node;
  node._curr = &first_writer;

  return a0_writer_write_impl(node, pkt);
}

A0_STATIC_INLINE
a0_err_t a0_compose_process_locked(void* user_data, a0_transport_locked_t tlk, a0_packet_t* pkt, a0_middleware_chain_t chain) {
  A0_MAYBE_UNUSED(tlk);
  return a0_compose_process(user_data, pkt, chain);
}

a0_err_t a0_middleware_compose(a0_middleware_t first, a0_middleware_t second, a0_middleware_t* out) {
  A0_RETURN_ERR_ON_ERR(a0_compose_init(first, second, &out->user_data));
  out->close = a0_compose_close;
  out->process = a0_compose_process;
  out->process_locked = a0_compose_process_locked;
  return A0_OK;
}
