#ifndef A0_PACKET_H
#define A0_PACKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct a0_buf_s {
  uint8_t* ptr;
  size_t size;
} a0_buf_t;

typedef struct a0_header_field_s {
  a0_buf_t key;
  a0_buf_t value;
} a0_header_field_t;

typedef struct a0_header_s {
  a0_header_field_t* fields;
  size_t num_fields;
} a0_header_t;

typedef struct a0_packet_s {
  a0_header_t header;
  a0_buf_t payload;
} a0_packet_t;

#endif  // A0_PACKET_H
