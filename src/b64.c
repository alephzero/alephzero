#include <a0/b64.h>

#include <errno.h>

// These implementation were selected by:
// https://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c#answer-41094722

////////////////////////////////////////////////////////////////////////////////////
//  Modified from http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c  //
////////////////////////////////////////////////////////////////////////////////////

/*
Base64 encoding/decoding (RFC1341)
Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>

License
-------

This software may be distributed, used, and modified under the terms of
BSD license:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name(s) of the above-listed copyright holder(s) nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

static const uint8_t kBase64EncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

errno_t b64_encode(const a0_buf_t* raw, a0_alloc_t* allocator, a0_buf_t* out_encoded) {
  allocator->callback(
      allocator->user_data,
      4 * ((raw->size + 2) / 3) /* 3-byte blocks to 4-byte */,
      out_encoded);

  const uint8_t* end = raw->ptr + raw->size;
  const uint8_t* in = raw->ptr;
  uint8_t* pos = out_encoded->ptr;
  while (end - in >= 3) {
    *pos++ = kBase64EncodeTable[in[0] >> 2];
    *pos++ = kBase64EncodeTable[((in[0] & 0x03) << 4) | (in[1] >> 4)];
    *pos++ = kBase64EncodeTable[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
    *pos++ = kBase64EncodeTable[in[2] & 0x3f];
    in += 3;
  }

  if (end - in) {
    *pos++ = kBase64EncodeTable[in[0] >> 2];
    if (end - in == 1) {
      *pos++ = kBase64EncodeTable[(in[0] & 0x03) << 4];
      *pos++ = '=';
    } else {
      *pos++ = kBase64EncodeTable[((in[0] & 0x03) << 4) | (in[1] >> 4)];
      *pos++ = kBase64EncodeTable[(in[1] & 0x0f) << 2];
    }
    *pos++ = '=';
  }

  return A0_OK;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Modified from https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c/13935718#answer-37109258  //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const int kBase64DecodeTable[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63, 52, 53, 54, 55, 56, 57,
  58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
  25, 0,  0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
  37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

errno_t b64_decode(const a0_buf_t* encoded, a0_alloc_t* allocator, a0_buf_t* out_decoded) {
  size_t pad1 = encoded->size % 4 || encoded->ptr[encoded->size - 1] == '=';
  size_t pad2 = pad1 && (encoded->size % 4 > 2 || encoded->ptr[encoded->size - 2] != '=');
  const size_t last = (encoded->size - pad1) / 4 << 2;
  allocator->callback(
      allocator->user_data,
      last / 4 * 3 + pad1 + pad2,
      out_decoded);

  size_t j = 0;

  for (size_t i = 0; i < last; i += 4) {
    int n = kBase64DecodeTable[encoded->ptr[i]] << 18 |
            kBase64DecodeTable[encoded->ptr[i + 1]] << 12 |
            kBase64DecodeTable[encoded->ptr[i + 2]] << 6 |
            kBase64DecodeTable[encoded->ptr[i + 3]];
    out_decoded->ptr[j++] = n >> 16;
    out_decoded->ptr[j++] = n >> 8 & 0xFF;
    out_decoded->ptr[j++] = n & 0xFF;
  }

  if (pad1) {
    int n = kBase64DecodeTable[encoded->ptr[last]] << 18 |
            kBase64DecodeTable[encoded->ptr[last + 1]] << 12;
    out_decoded->ptr[j++] = n >> 16;

    if (pad2) {
      n |= kBase64DecodeTable[encoded->ptr[last + 2]] << 6;
      out_decoded->ptr[j++] = n >> 8 & 0xFF;
    }
  }

  return A0_OK;
}
