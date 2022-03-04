#ifndef A0_CFG_H
#define A0_CFG_H

#include <a0/err.h>
#include <a0/file.h>
#include <a0/reader.h>
#include <a0/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_cfg_topic_s {
  const char* name;
  const a0_file_options_t* file_opts;
} a0_cfg_topic_t;

typedef struct a0_cfg_s {
  a0_file_t _file;
  a0_writer_t _writer;
} a0_cfg_t;

a0_err_t a0_cfg_init(a0_cfg_t*, a0_cfg_topic_t);

a0_err_t a0_cfg_close(a0_cfg_t*);

a0_err_t a0_cfg_read(a0_cfg_t*, a0_alloc_t, a0_packet_t* out);
a0_err_t a0_cfg_read_blocking(a0_cfg_t*, a0_alloc_t, a0_packet_t* out);
a0_err_t a0_cfg_read_blocking_timeout(a0_cfg_t*, a0_alloc_t, a0_time_mono_t*, a0_packet_t* out);

a0_err_t a0_cfg_write(a0_cfg_t*, a0_packet_t);
a0_err_t a0_cfg_write_if_empty(a0_cfg_t*, a0_packet_t, bool* written);

a0_err_t a0_cfg_mergepatch(a0_cfg_t*, a0_packet_t);

typedef struct a0_cfg_watcher_s {
  a0_file_t _file;
  a0_reader_t _reader;
} a0_cfg_watcher_t;

a0_err_t a0_cfg_watcher_init(a0_cfg_watcher_t*, a0_cfg_topic_t, a0_alloc_t, a0_packet_callback_t);

a0_err_t a0_cfg_watcher_close(a0_cfg_watcher_t*);

#ifdef __cplusplus
}
#endif

#endif  // A0_CFG_H
