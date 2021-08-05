#ifndef A0_SRC_PROTOCOL_UTIL_H
#define A0_SRC_PROTOCOL_UTIL_H

#include <a0/callback.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

errno_t a0_open_topic(const char* template,
                      const char* topic,
                      const a0_file_options_t* topic_opts,
                      a0_file_t* file);

errno_t a0_find_header(a0_packet_t pkt, const char* key, const char** val);

extern const a0_hash_t A0_UUID_HASH;
extern const a0_compare_t A0_UUID_COMPARE;

#endif  // A0_SRC_PROTOCOL_UTIL_H
