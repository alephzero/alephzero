#ifndef A0_TOPIC_H
#define A0_TOPIC_H

#include <a0/err.h>
#include <a0/file.h>

#ifdef __cplusplus
extern "C" {
#endif

a0_err_t a0_topic_path(const char* tmpl,
                       const char* topic,
                       const char** path);

a0_err_t a0_topic_open(const char* tmpl,
                       const char* topic,
                       const a0_file_options_t* topic_opts,
                       a0_file_t* file);

#ifdef __cplusplus
}
#endif

#endif  // A0_TOPIC_H
