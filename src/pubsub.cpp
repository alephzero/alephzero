// #include <a0/alloc.h>
// #include <a0/arena.h>
// #include <a0/callback.h>
// #include <a0/err.h>
// #include <a0/packet.h>
// #include <a0/pubsub.h>
// #include <a0/time.h>
// #include <a0/transport.h>
// #include <a0/uuid.h>
// #include <a0/writer.h>

// #include <fcntl.h>

// #include <cerrno>
// #include <cstdint>
// #include <ctime>
// #include <memory>
// #include <string_view>

// #include "alloc_util.hpp"
// #include "err_util.h"
// #include "scope.hpp"
// #include "strconv.h"
// #include "sync.hpp"
// #include "transport_tools.hpp"

// #ifdef DEBUG
// #include "assert.h"
// #include "ref_cnt.h"
// #endif


// A0_STATIC_INLINE
// errno_t open_topic(const char* topic,
//                    const a0_file_options_t* topic_opts,
//                    a0_file_t* file) {
//   if (topic[0] == '\0' || topic[0] == '/') {
//     return EINVAL;
//   }

//   const char* TEMPLATE_PRE = "alephzero/";
//   const char* TEMPLATE_POST = ".pubsub.a0";
//   char* path = (char*)alloca(strlen(TEMPLATE_PRE) + strlen(topic) + strlen(TEMPLATE_POST) + 1);

//   size_t off = 0;
//   memcpy(path + off, TEMPLATE_PRE, strlen(TEMPLATE_PRE));
//   off += strlen(TEMPLATE_PRE);
//   memcpy(path + off, topic, strlen(topic));
//   off += strlen(topic);
//   memcpy(path + off, TEMPLATE_POST, strlen(TEMPLATE_POST));
//   off += strlen(TEMPLATE_POST);
//   path[off] = '\0';

//   return a0_file_open(path, topic_opts, file);
// }

// /////////////////
// //  Publisher  //
// /////////////////

// errno_t a0_publisher_init(
//     a0_publisher_t* pub,
//     const char* topic,
//     const a0_file_options_t* topic_opts) {
//   A0_RETURN_ERR_ON_ERR(open_topic(topic, topic_opts, &pub->_file));

//   errno_t err = a0_writer_init(&pub->_simple_writer, pub->_file.arena);
//   if (err) {
//     a0_file_close(&pub->_file);
//     return err;
//   }

//   err = a0_writer_wrap(
//       &pub->_simple_writer,
//       a0_writer_middleware_add_standard_headers(),
//       &pub->_annotated_writer);
//   if (err) {
//     a0_writer_close(&pub->_simple_writer);
//     a0_file_close(&pub->_file);
//     return err;
//   }

//   return A0_OK;
// }

// errno_t a0_publisher_close(a0_publisher_t* pub) {
//   a0_writer_close(&pub->_annotated_writer);
//   a0_writer_close(&pub->_simple_writer);
//   a0_file_close(&pub->_file);
//   return A0_OK;
// }

// errno_t a0_publisher_pub(a0_publisher_t* pub, a0_packet_t pkt) {
//   return a0_writer_write(&pub->_annotated_writer, pkt);
// }

// //////////////////
// //  Subscriber  //
// //////////////////

// // Synchronous zero-copy version.

// errno_t a0_subscriber_sync_zc_init(a0_subscriber_sync_zc_t* sub_sync_zc,
//                                    const char* topic,
//                                    const a0_file_options_t* topic_opts,
//                                    a0_reader_init_t init,
//                                    a0_reader_iter_t iter) {
//   A0_RETURN_ERR_ON_ERR(open_topic(topic, topic_opts, &sub_sync_zc->_file));

//   errno_t err = a0_reader_sync_zc_init(
//       &sub_sync_zc->_reader_sync_zc,
//       sub_sync_zc->_file.arena,
//       init,
//       iter);
//   if (err) {
//     a0_file_close(&sub_sync_zc->_file);
//     return err;
//   }

//   return A0_OK;
// }

// errno_t a0_subscriber_sync_zc_close(a0_subscriber_sync_zc_t* sub_sync_zc) {
//   a0_reader_sync_zc_close(&sub_sync_zc->_reader_sync_zc);
//   a0_file_close(&sub_sync_zc->_file);
//   return A0_OK;
// }

// errno_t a0_subscriber_sync_zc_has_next(a0_subscriber_sync_zc_t* sub_sync_zc, bool* has_next) {
//   return a0_reader_sync_zc_has_next(&sub_sync_zc->_reader_sync_zc, has_next);
// }

// errno_t a0_subscriber_sync_zc_next(a0_subscriber_sync_zc_t* sub_sync_zc,
//                                    a0_zero_copy_callback_t cb) {
//   return a0_reader_sync_zc_next(&sub_sync_zc->_reader_sync_zc, cb);
// }

// // Synchronous allocated version.

// errno_t a0_subscriber_sync_init(a0_subscriber_sync_t* sub_sync,
//                                 const char* topic,
//                                 const a0_file_options_t* topic_opts,
//                                 a0_alloc_t alloc,
//                                 a0_reader_init_t init,
//                                 a0_reader_iter_t iter) {
//   A0_RETURN_ERR_ON_ERR(open_topic(topic, topic_opts, &sub_sync->_file));

//   errno_t err = a0_reader_sync_init(
//       &sub_sync->_reader_sync,
//       sub_sync->_file.arena,
//       alloc,
//       init,
//       iter);
//   if (err) {
//     a0_file_close(&sub_sync->_file);
//     return err;
//   }

//   return A0_OK;
// }

// errno_t a0_subscriber_sync_close(a0_subscriber_sync_t* sub_sync) {
//   a0_reader_sync_close(&sub_sync->_reader_sync);
//   a0_file_close(&sub_sync->_file);
//   return A0_OK;
// }

// errno_t a0_subscriber_sync_has_next(a0_subscriber_sync_t* sub_sync, bool* has_next) {
//   return a0_reader_sync_has_next(&sub_sync->_reader_sync, has_next);
// }

// errno_t a0_subscriber_sync_next(a0_subscriber_sync_t* sub_sync, a0_packet_t* pkt) {
//   return a0_reader_sync_next(&sub_sync->_reader_sync, pkt);
// }

// // Zero-copy threaded version.

// struct a0_subscriber_zc_impl_s {
//   a0::transport_thread worker;
//   bool started_empty;
// };

// errno_t a0_subscriber_zc_init(a0_subscriber_zc_t* sub_zc,
//                               a0_arena_t arena,
//                               a0_reader_init_t sub_init,
//                               a0_reader_iter_t sub_iter,
//                               a0_zero_copy_callback_t onmsg) {
//   sub_zc->_impl = new a0_subscriber_zc_impl_t;

//   auto on_transport_init = [sub_zc, sub_init](a0_locked_transport_t tlk) -> errno_t {
//     // TODO(lshamis): Validate transport.

//     a0_transport_empty(tlk, &sub_zc->_impl->started_empty);
//     if (!sub_zc->_impl->started_empty) {
//       if (sub_init == A0_INIT_OLDEST) {
//         a0_transport_jump_head(tlk);
//       } else if (sub_init == A0_INIT_MOST_RECENT || sub_init == A0_INIT_AWAIT_NEW) {
//         a0_transport_jump_tail(tlk);
//       }
//     }

//     return A0_OK;
//   };

//   auto handle_pkt = [onmsg](a0_locked_transport_t tlk) {
//     a0_transport_frame_t frame;
//     a0_transport_frame(tlk, &frame);
//     onmsg.fn(onmsg.user_data, tlk, a0::buf(frame));
//   };

//   auto on_transport_nonempty = [sub_zc, sub_init, handle_pkt](a0_locked_transport_t tlk) {
//     bool reset = false;
//     if (sub_zc->_impl->started_empty) {
//       reset = true;
//     } else {
//       bool ptr_valid;
//       a0_transport_ptr_valid(tlk, &ptr_valid);
//       reset = !ptr_valid;
//     }

//     if (reset) {
//       a0_transport_jump_head(tlk);
//     }

//     if (reset || sub_init == A0_INIT_OLDEST || sub_init == A0_INIT_MOST_RECENT) {
//       handle_pkt(tlk);
//     }
//   };

//   auto on_transport_hasnext = [sub_iter, handle_pkt](a0_locked_transport_t tlk) {
//     if (sub_iter == A0_ITER_NEXT) {
//       a0_transport_next(tlk);
//     } else if (sub_iter == A0_ITER_NEWEST) {
//       a0_transport_jump_tail(tlk);
//     }

//     handle_pkt(tlk);
//   };

//   return sub_zc->_impl->worker.init(arena,
//                                     on_transport_init,
//                                     on_transport_nonempty,
//                                     on_transport_hasnext);
// }

// errno_t a0_subscriber_zc_async_close(a0_subscriber_zc_t* sub_zc, a0_callback_t onclose) {
//   if (!sub_zc || !sub_zc->_impl) {
//     return ESHUTDOWN;
//   }

//   sub_zc->_impl->worker.async_close([sub_zc, onclose]() {
//     delete sub_zc->_impl;
//     sub_zc->_impl = nullptr;

//     if (onclose.fn) {
//       onclose.fn(onclose.user_data);
//     }
//   });

//   return A0_OK;
// }

// errno_t a0_subscriber_zc_close(a0_subscriber_zc_t* sub_zc) {
//   if (!sub_zc || !sub_zc->_impl) {
//     return ESHUTDOWN;
//   }

//   sub_zc->_impl->worker.await_close();
//   delete sub_zc->_impl;
//   sub_zc->_impl = nullptr;

//   return A0_OK;
// }

// // Normal threaded version.

// struct a0_subscriber_impl_s {
//   a0_subscriber_zc_t sub_zc;

//   a0_alloc_t alloc;
//   a0_packet_callback_t onmsg;
// };

// errno_t a0_subscriber_init(a0_subscriber_t* sub,
//                            a0_arena_t arena,
//                            a0_alloc_t alloc,
//                            a0_reader_init_t sub_init,
//                            a0_reader_iter_t sub_iter,
//                            a0_packet_callback_t onmsg) {
//   sub->_impl = new a0_subscriber_impl_t;

//   sub->_impl->alloc = alloc;
//   sub->_impl->onmsg = onmsg;

//   a0_zero_copy_callback_t wrapped_onmsg = {
//       .user_data = sub->_impl,
//       .fn =
//           [](void* data, a0_locked_transport_t tlk, a0_flat_packet_t pkt_zc) {
//             auto* impl = (a0_subscriber_impl_t*)data;
//             a0_packet_t pkt;
//             a0_packet_deserialize(pkt_zc, impl->alloc, &pkt);

//             a0::scoped_transport_unlock stulk(tlk);
//             impl->onmsg.fn(impl->onmsg.user_data, pkt);
//           },
//   };

//   return a0_subscriber_zc_init(&sub->_impl->sub_zc, arena, sub_init, sub_iter, wrapped_onmsg);
// }

// errno_t a0_subscriber_close(a0_subscriber_t* sub) {
//   if (!sub || !sub->_impl) {
//     return ESHUTDOWN;
//   }

//   auto err = a0_subscriber_zc_close(&sub->_impl->sub_zc);
//   delete sub->_impl;
//   sub->_impl = nullptr;

//   return err;
// }

// errno_t a0_subscriber_async_close(a0_subscriber_t* sub, a0_callback_t onclose) {
//   if (!sub || !sub->_impl) {
//     return ESHUTDOWN;
//   }

//   struct heap_data {
//     a0_subscriber_t* sub_;
//     a0_callback_t onclose_;
//   };

//   a0_callback_t cb = {
//       .user_data = new heap_data{sub, onclose},
//       .fn = [](void* user_data) {
//         auto* data = (heap_data*)user_data;
//         delete data->sub_->_impl;
//         data->sub_->_impl = nullptr;
//         if (data->onclose_.fn) {
//           data->onclose_.fn(data->onclose_.user_data);
//         }
//         delete data;
//       },
//   };

//   // clang-tidy thinks the new heap_data is a leak.
//   // It can't track it through the callback.
//   // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
//   return a0_subscriber_zc_async_close(&sub->_impl->sub_zc, cb);
// }

// // One-off reader.

// errno_t a0_subscriber_read_one(a0_arena_t arena,
//                                a0_alloc_t alloc,
//                                a0_reader_init_t sub_init,
//                                int flags,
//                                a0_packet_t* out) {
//   if (flags & O_NDELAY || flags & O_NONBLOCK) {
//     if (sub_init == A0_INIT_AWAIT_NEW) {
//       return EAGAIN;
//     }

//     a0_reader_sync_t sub_sync;
//     A0_RETURN_ERR_ON_ERR(a0_reader_sync_init(&sub_sync, arena, alloc, sub_init, A0_ITER_NEXT));
//     struct sub_guard {
//       a0_reader_sync_t* sub_sync;
//       ~sub_guard() {
//         a0_reader_sync_close(sub_sync);
//       }
//     } sub_guard_{&sub_sync};

//     bool has_next;
//     A0_RETURN_ERR_ON_ERR(a0_reader_sync_has_next(&sub_sync, &has_next));
//     if (!has_next) {
//       return EAGAIN;
//     }
//     A0_RETURN_ERR_ON_ERR(a0_reader_sync_next(&sub_sync, out));
//   } else {
//     struct data_ {
//       a0_packet_t* pkt;

//       a0::Event sub_event{};
//       a0::Event done_event{};
//     } data{.pkt = out};

//     a0_packet_callback_t cb = {
//         .user_data = &data,
//         .fn =
//             [](void* user_data, a0_packet_t pkt) {
//               auto* data = (data_*)user_data;
//               if (data->done_event.is_set()) {
//                 return;
//               }

//               data->sub_event.wait();
//               *data->pkt = pkt;
//               data->done_event.set();
//             },
//     };

//     a0_reader_t sub;
//     A0_RETURN_ERR_ON_ERR(a0_reader_init(&sub, arena, alloc, sub_init, A0_ITER_NEXT, cb));

//     data.sub_event.set();
//     data.done_event.wait();

//     A0_RETURN_ERR_ON_ERR(a0_reader_close(&sub));
//   }

//   return A0_OK;
// }
