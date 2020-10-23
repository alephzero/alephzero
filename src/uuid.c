#include <a0/uuid.h>

#include <stdint.h>
#include <string.h>

#include "rand.h"

static const char HEX_DIGITS[] =
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

void a0_uuidv4(a0_uuid_t out) {
  uint32_t data[4] = {
      (uint32_t)a0_mrand48(),
      ((uint32_t)a0_mrand48() & 0xFF0FFFFF) | 0x00400000,
      ((uint32_t)a0_mrand48() & 0xFFFFFF3F) | 0x00000080,
      (uint32_t)a0_mrand48(),
  };

  uint8_t* bytes = (uint8_t*)data;

  memcpy(&out[0], &HEX_DIGITS[bytes[0] * 2], sizeof(uint16_t));
  memcpy(&out[2], &HEX_DIGITS[bytes[1] * 2], sizeof(uint16_t));
  memcpy(&out[4], &HEX_DIGITS[bytes[2] * 2], sizeof(uint16_t));
  memcpy(&out[6], &HEX_DIGITS[bytes[3] * 2], sizeof(uint16_t));
  out[8] = '-';
  memcpy(&out[9], &HEX_DIGITS[bytes[4] * 2], sizeof(uint16_t));
  memcpy(&out[11], &HEX_DIGITS[bytes[5] * 2], sizeof(uint16_t));
  out[13] = '-';
  memcpy(&out[14], &HEX_DIGITS[bytes[6] * 2], sizeof(uint16_t));
  memcpy(&out[16], &HEX_DIGITS[bytes[7] * 2], sizeof(uint16_t));
  out[18] = '-';
  memcpy(&out[19], &HEX_DIGITS[bytes[8] * 2], sizeof(uint16_t));
  memcpy(&out[21], &HEX_DIGITS[bytes[9] * 2], sizeof(uint16_t));
  out[23] = '-';
  memcpy(&out[24], &HEX_DIGITS[bytes[10] * 2], sizeof(uint16_t));
  memcpy(&out[26], &HEX_DIGITS[bytes[11] * 2], sizeof(uint16_t));
  memcpy(&out[28], &HEX_DIGITS[bytes[12] * 2], sizeof(uint16_t));
  memcpy(&out[30], &HEX_DIGITS[bytes[13] * 2], sizeof(uint16_t));
  memcpy(&out[32], &HEX_DIGITS[bytes[14] * 2], sizeof(uint16_t));
  memcpy(&out[34], &HEX_DIGITS[bytes[15] * 2], sizeof(uint16_t));
  out[36] = 0;
}
