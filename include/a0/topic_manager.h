#ifndef A0_TOPIC_MANAGER_H
#define A0_TOPIC_MANAGER_H

#include <a0/common.h>  // for errno_t, A0_OK
#include <a0/shm.h>     // for a0_shm_open, a0_shm_t, a0_shm_options_t

#include <errno.h>  // for EINVAL, ESHUTDOWN

#ifdef __cplusplus
extern "C" {
#endif

// Topic manager schema:
// {
//   "type": "object",
//   "properties": {
//     "container": { "type": "string" },
//   },
//   "patternProperties": {
//     "subscriber_maps|rpc_client_maps": {
//       "type": "object",
//       "patternProperties": {
//         ".*": {
//           "type": "object",
//           "properties": {
//             "container": { "type": "string" },
//             "topic": { "type": "string" }
//           },
//           "required": [ "container", "topic" ]
//         }
//       }
//     },
//   },
//   "additionalProperties": false,
//   "required": [ "container" ]
// }

typedef struct a0_topic_manager_impl_s a0_topic_manager_impl_t;

typedef struct a0_topic_manager_s {
  a0_topic_manager_impl_t* _impl;
} a0_topic_manager_t;

errno_t a0_topic_manager_init(a0_topic_manager_t*, const char* json);
errno_t a0_topic_manager_close(a0_topic_manager_t*);

errno_t a0_topic_manager_open_config_topic(a0_topic_manager_t*, a0_shm_t* out);
errno_t a0_topic_manager_open_publisher_topic(a0_topic_manager_t*, const char*, a0_shm_t* out);
errno_t a0_topic_manager_open_subscriber_topic(a0_topic_manager_t*, const char*, a0_shm_t* out);
errno_t a0_topic_manager_open_rpc_server_topic(a0_topic_manager_t*, const char*, a0_shm_t* out);
errno_t a0_topic_manager_open_rpc_client_topic(a0_topic_manager_t*, const char*, a0_shm_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_TOPIC_MANAGER_H
