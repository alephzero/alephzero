#ifndef A0_SRC_PROTOCOL_UTIL_H
#define A0_SRC_PROTOCOL_UTIL_H

#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>

#ifdef __cplusplus
extern "C" {
#endif

errno_t a0_open_topic(const char* tmpl,
                      const char* topic,
                      const a0_file_options_t* topic_opts,
                      a0_file_t* file);

errno_t a0_find_header(a0_packet_t pkt, const char* key, const char** val);

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_PROTOCOL_UTIL_H
