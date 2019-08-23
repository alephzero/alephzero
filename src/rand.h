#ifndef A0_INTERNAL_RAND_H
#define A0_INTERNAL_RAND_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

extern __thread unsigned short a0_xsubi[3];
extern __thread bool a0_xsubi_init;

A0_STATIC_INLINE long int a0_mrand48() {
  if (A0_UNLIKELY(!a0_xsubi_init)) {
    // TODO: error handling.
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t todo_use_this = read(fd, a0_xsubi, sizeof(a0_xsubi));
    (void)todo_use_this;
    close(fd);
    a0_xsubi_init = true;
  }
  return jrand48(a0_xsubi);
}

static const size_t kUuidSize = 37;

static const char kHexDigits[] =
    "000102030405060708090A0B0C0D0E0F"
    "101112131415161718191A1B1C1D1E1F"
    "202122232425262728292A2B2C2D2E2F"
    "303132333435363738393A3B3C3D3E3F"
    "404142434445464748494A4B4C4D4E4F"
    "505152535455565758595A5B5C5D5E5F"
    "606162636465666768696A6B6C6D6E6F"
    "707172737475767778797A7B7C7D7E7F"
    "808182838485868788898A8B8C8D8E8F"
    "909192939495969798999A9B9C9D9E9F"
    "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"
    "B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
    "C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF"
    "D0D1D2D3D4D5D6D7D8D9DADBDCDDDEDF"
    "E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF"
    "F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";

A0_STATIC_INLINE
void uuidv4(uint8_t out[37]) {
  uint32_t data[4] = {
      (uint32_t)a0_mrand48(),
      ((uint32_t)a0_mrand48() & 0xFF0FFFFF) | 0x00400000,
      ((uint32_t)a0_mrand48() & 0xFFFFFF3F) | 0x00000080,
      (uint32_t)a0_mrand48(),
  };

  uint8_t* bytes = (uint8_t*)data;

// GCC 5.4.0 complains about this.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
  *(uint16_t*)(&out[0]) = *(uint16_t*)(&kHexDigits[bytes[0] * 2]);
  *(uint16_t*)(&out[2]) = *(uint16_t*)(&kHexDigits[bytes[1] * 2]);
  *(uint16_t*)(&out[4]) = *(uint16_t*)(&kHexDigits[bytes[2] * 2]);
  *(uint16_t*)(&out[6]) = *(uint16_t*)(&kHexDigits[bytes[3] * 2]);
  out[8] = '-';
  *(uint16_t*)(&out[9]) = *(uint16_t*)(&kHexDigits[bytes[4] * 2]);
  *(uint16_t*)(&out[11]) = *(uint16_t*)(&kHexDigits[bytes[5] * 2]);
  out[13] = '-';
  *(uint16_t*)(&out[14]) = *(uint16_t*)(&kHexDigits[bytes[6] * 2]);
  *(uint16_t*)(&out[16]) = *(uint16_t*)(&kHexDigits[bytes[7] * 2]);
  out[18] = '-';
  *(uint16_t*)(&out[19]) = *(uint16_t*)(&kHexDigits[bytes[8] * 2]);
  *(uint16_t*)(&out[21]) = *(uint16_t*)(&kHexDigits[bytes[9] * 2]);
  out[23] = '-';
  *(uint16_t*)(&out[24]) = *(uint16_t*)(&kHexDigits[bytes[10] * 2]);
  *(uint16_t*)(&out[26]) = *(uint16_t*)(&kHexDigits[bytes[11] * 2]);
  *(uint16_t*)(&out[28]) = *(uint16_t*)(&kHexDigits[bytes[12] * 2]);
  *(uint16_t*)(&out[30]) = *(uint16_t*)(&kHexDigits[bytes[13] * 2]);
  *(uint16_t*)(&out[32]) = *(uint16_t*)(&kHexDigits[bytes[14] * 2]);
  *(uint16_t*)(&out[34]) = *(uint16_t*)(&kHexDigits[bytes[15] * 2]);
  out[36] = 0;
#pragma GCC diagnostic pop
}

#ifdef __cplusplus
}
#endif

#endif  // A0_INTERNAL_RAND_H
