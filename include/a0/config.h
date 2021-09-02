#ifndef A0_CONFIG_H
#define A0_CONFIG_H

#include <a0/err.h>
#include <a0/file.h>
#include <a0/reader.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_config_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_config_topic_t;

a0_err_t a0_config(a0_config_topic_t, a0_alloc_t, int flags, a0_packet_t* out);

a0_err_t a0_write_config(a0_config_topic_t, a0_packet_t);

typedef struct a0_onconfig_s {
  a0_file_t _file;
  a0_reader_t _reader;
} a0_onconfig_t;

a0_err_t a0_onconfig_init(a0_onconfig_t*, a0_config_topic_t, a0_alloc_t, a0_packet_callback_t);

a0_err_t a0_onconfig_close(a0_onconfig_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_CONFIG_H
