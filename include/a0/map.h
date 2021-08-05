#ifndef A0_MAP_H
#define A0_MAP_H

#include <a0/callback.h>
#include <a0/err.h>
#include <a0/inline.h>

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct a0_map_s {
  size_t _key_size;
  size_t _val_size;

  a0_hash_t _key_hash;
  a0_compare_t _key_compare;

  size_t _size;
  size_t _cap;

  size_t _hash2idx;

  uint8_t* _data;

  size_t _bucket_size;
} a0_map_t;

errno_t a0_map_init(a0_map_t*,
                    size_t key_size,
                    size_t val_size,
                    a0_hash_t key_hash,
                    a0_compare_t key_compare);

errno_t a0_map_close(a0_map_t*);

errno_t a0_map_empty(a0_map_t*, bool* is_empty);

errno_t a0_map_size(a0_map_t*, size_t* size);

errno_t a0_map_has(a0_map_t*, const void* key, bool* contains);

errno_t a0_map_put(a0_map_t*, const void* key, const void* val);

errno_t a0_map_del(a0_map_t*, const void* key);

errno_t a0_map_get(a0_map_t*, const void* key, void** val);

errno_t a0_map_pop(a0_map_t*, const void* key, void* val);

typedef struct a0_map_iterator_s {
  a0_map_t* _map;
  size_t _idx;
} a0_map_iterator_t;

errno_t a0_map_iterator_init(a0_map_iterator_t*, a0_map_t*);

errno_t a0_map_iterator_next(a0_map_iterator_t*, const void** key, void** val);

#ifdef __cplusplus
}
#endif

#endif  // A0_MAP_H
