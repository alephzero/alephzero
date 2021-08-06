#include "protocol_util.h"

#include <a0/err.h>
#include <a0/file.h>
#include <a0/packet.h>

#include <alloca.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "err_util.h"

errno_t a0_open_topic(const char* template,
                      const char* topic,
                      const a0_file_options_t* topic_opts,
                      a0_file_t* file) {
  if (topic[0] == '\0' || topic[0] == '/') {
    return EINVAL;
  }

  const size_t topic_len = strlen(topic);

  static const size_t MAX_MATCH_CNT = 4;
  const char* segment_starts[MAX_MATCH_CNT];
  size_t segment_size[MAX_MATCH_CNT];
  uint8_t num_matches = 0;

  size_t len = 0;
  const char* prev = template;
  const char* next;
  while ((next = strstr(prev, "{topic}")) && num_matches < MAX_MATCH_CNT) {
    segment_starts[num_matches] = prev;
    segment_size[num_matches] = next - prev;
    num_matches++;

    len += next - prev + topic_len;
    prev = next + strlen("{topic}");
  }
  size_t tail_len = strlen(prev);
  len += tail_len;

  char* path = (char*)alloca(len + 1);
  char* write_ptr = path;
  for (size_t i = 0; i < num_matches; ++i) {
    strncpy(write_ptr, segment_starts[i], segment_size[i]);
    write_ptr += segment_size[i];
    strcpy(write_ptr, topic);
    write_ptr += topic_len;
  }
  strcpy(write_ptr, prev);
  write_ptr += tail_len;
  *write_ptr = '\0';
  return a0_file_open(path, topic_opts, file);
}

errno_t a0_find_header(a0_packet_t pkt, const char* key, const char** val) {
  a0_packet_header_iterator_t iter;
  A0_RETURN_ERR_ON_ERR(a0_packet_header_iterator_init(
      &iter, &pkt.headers_block));

  a0_packet_header_t hdr;
  while (a0_packet_header_iterator_next(&iter, &hdr) == A0_OK) {
    if (!strcmp(hdr.key, key)) {
      *val = hdr.val;
      return A0_OK;
    }
  }
  return EINVAL;
}
