/**
 * \file buf.h
 * \rst
 *
 * A buf can be any contiguous memory buffer.
 *
 * .. code-block:: cpp
 *
 *   a0::Buf buf(data_ptr, date_len);
 *   buf.data();
 *   buf.size();
 *
 * \endrst
 */

#ifndef A0_BUF_H
#define A0_BUF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A buf can be any contiguous memory buffer.
typedef struct a0_buf_s {
  /// Pointer to the contiguous memory buffer.
  uint8_t* data;
  /// Size of the buffer in bytes.
  size_t size;
} a0_buf_t;

#ifdef __cplusplus
}
#endif

#endif  // A0_BUF_H
