#include <a0/compare.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/map.h>

#include <alloca.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "err_util.h"

typedef struct _a0_map_bucket_s {
  void* ptr;
  size_t idx;
  // Zero indicates empty bucket.
  // Distance measure is one-indexed.
  size_t* off;
  void* key;
  void* val;
} _a0_map_bucket_t;

errno_t a0_map_init(a0_map_t* map,
                    size_t key_size,
                    size_t val_size,
                    a0_hash_t key_hash,
                    a0_compare_t key_compare) {
  memset(map, 0, sizeof(a0_map_t));
  map->_key_size = key_size;
  map->_val_size = val_size;
  map->_key_hash = key_hash;
  map->_key_compare = key_compare;

  map->_bucket_size = sizeof(size_t) + map->_key_size + map->_val_size;

  return A0_OK;
}

errno_t a0_map_close(a0_map_t* map) {
  if (map->_data) {
    free(map->_data);
  }
  memset(map, 0, sizeof(a0_map_t));
  return A0_OK;
}

errno_t a0_map_empty(a0_map_t* map, bool* is_empty) {
  *is_empty = (map->_size == 0);
  return A0_OK;
}

errno_t a0_map_size(a0_map_t* map, size_t* size) {
  *size = map->_size;
  return A0_OK;
}

A0_STATIC_INLINE
errno_t _a0_map_bucket(a0_map_t* map, size_t idx, _a0_map_bucket_t* bkt) {
  bkt->ptr = map->_data + idx * map->_bucket_size;
  bkt->idx = idx;
  bkt->off = (size_t*)bkt->ptr;
  bkt->key = (uint8_t*)(bkt->off) + sizeof(size_t);
  bkt->val = (uint8_t*)(bkt->key) + map->_key_size;
  return A0_OK;
}

A0_STATIC_INLINE
errno_t _a0_map_find(a0_map_t* map, const void* key, _a0_map_bucket_t* bkt) {
  if (!map->_size) {
    return EINVAL;
  }

  size_t hash;
  A0_RETURN_ERR_ON_ERR(a0_hash_eval(map->_key_hash, key, &hash));
  size_t idx = hash & map->_hash2idx;

  size_t off = 1;
  A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, idx, bkt));

  while (off <= *bkt->off) {
    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_compare_eval(map->_key_compare, key, bkt->key, &cmp));
    if (!cmp) {
      return A0_OK;
    }

    idx = (idx + 1) & map->_hash2idx;
    A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, idx, bkt));
    off++;
  }

  return EINVAL;
}

errno_t a0_map_has(a0_map_t* map, const void* key, bool* contains) {
  _a0_map_bucket_t bkt;
  *contains = (_a0_map_find(map, key, &bkt) == A0_OK);
  return A0_OK;
}

A0_STATIC_INLINE
errno_t _a0_map_grow(a0_map_t* map) {
  a0_map_t new_map = *map;
  new_map._size = 0;
  new_map._cap = new_map._cap ? (new_map._cap * 2) : 4;
  new_map._hash2idx = new_map._cap - 1;
  new_map._data = (uint8_t*)malloc(new_map._cap * new_map._bucket_size);
  memset(new_map._data, 0, new_map._cap * new_map._bucket_size);

  a0_map_iterator_t iter;
  A0_RETURN_ERR_ON_ERR(a0_map_iterator_init(&iter, map));
  const void* key;
  void* val;
  while (a0_map_iterator_next(&iter, &key, &val) == A0_OK) {
    A0_RETURN_ERR_ON_ERR(a0_map_put(&new_map, key, val));
  }
  free(map->_data);
  *map = new_map;
  return A0_OK;
}

errno_t a0_map_put(a0_map_t* map, const void* key, const void* val) {
  if (map->_size * 2 >= map->_cap) {
    A0_RETURN_ERR_ON_ERR(_a0_map_grow(map));
  }

  size_t hash;
  A0_RETURN_ERR_ON_ERR(a0_hash_eval(map->_key_hash, key, &hash));

  _a0_map_bucket_t new_bkt;
  new_bkt.ptr = alloca(map->_bucket_size);
  new_bkt.idx = hash & map->_hash2idx;
  new_bkt.off = (size_t*)new_bkt.ptr;
  new_bkt.key = (uint8_t*)(new_bkt.off) + sizeof(size_t);
  new_bkt.val = (uint8_t*)(new_bkt.key) + map->_key_size;

  *new_bkt.off = 1;
  memcpy(new_bkt.key, key, map->_key_size);
  memcpy(new_bkt.val, val, map->_val_size);

  _a0_map_bucket_t iter_bkt;
  A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, new_bkt.idx, &iter_bkt));

  while (*iter_bkt.off) {
    int cmp;
    A0_RETURN_ERR_ON_ERR(a0_compare_eval(map->_key_compare, new_bkt.key, iter_bkt.key, &cmp));
    if (!cmp) {
      memcpy(iter_bkt.val, new_bkt.val, map->_val_size);
      return A0_OK;
    }

    if (*new_bkt.off > *iter_bkt.off) {
      void* tmp_bkt = alloca(map->_bucket_size);
      memcpy(tmp_bkt, iter_bkt.ptr, map->_bucket_size);
      memcpy(iter_bkt.ptr, new_bkt.ptr, map->_bucket_size);
      memcpy(new_bkt.ptr, tmp_bkt, map->_bucket_size);
    }

    new_bkt.idx = (new_bkt.idx + 1) & map->_hash2idx;
    A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, new_bkt.idx, &iter_bkt));
    (*new_bkt.off)++;
  }

  memcpy(iter_bkt.ptr, new_bkt.ptr, map->_bucket_size);
  map->_size++;

  return A0_OK;
}

A0_STATIC_INLINE
errno_t _a0_map_del_bucket(a0_map_t* map, _a0_map_bucket_t bkt) {
  *bkt.off = 0;

  map->_size--;

  size_t next_idx = (bkt.idx + 1) & map->_hash2idx;

  _a0_map_bucket_t next_bkt;
  A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, next_idx, &next_bkt));

  while (*next_bkt.off > 1) {
    memcpy(bkt.ptr, next_bkt.ptr, map->_bucket_size);
    (*bkt.off)--;
    *next_bkt.off = 0;

    bkt = next_bkt;

    next_idx = (bkt.idx + 1) & map->_hash2idx;
    A0_RETURN_ERR_ON_ERR(_a0_map_bucket(map, next_idx, &next_bkt));
  }

  return A0_OK;
}

errno_t a0_map_del(a0_map_t* map, const void* key) {
  if (!map->_size) {
    return EINVAL;
  }

  _a0_map_bucket_t bkt;
  A0_RETURN_ERR_ON_ERR(_a0_map_find(map, key, &bkt));
  return _a0_map_del_bucket(map, bkt);
}

errno_t a0_map_get(a0_map_t* map, const void* key, void** val) {
  _a0_map_bucket_t bkt;
  A0_RETURN_ERR_ON_ERR(_a0_map_find(map, key, &bkt));
  *val = bkt.val;
  return A0_OK;
}

errno_t a0_map_pop(a0_map_t* map, const void* key, void* val) {
  _a0_map_bucket_t bkt;
  A0_RETURN_ERR_ON_ERR(_a0_map_find(map, key, &bkt));
  memcpy(val, bkt.val, map->_val_size);
  return _a0_map_del_bucket(map, bkt);
}

errno_t a0_map_iterator_init(a0_map_iterator_t* iter, a0_map_t* map) {
  *iter = (a0_map_iterator_t){
      ._map = map,
      ._idx = 0,
  };
  return A0_OK;
}

errno_t a0_map_iterator_next(a0_map_iterator_t* iter, const void** key, void** val) {
  if (iter->_idx >= iter->_map->_cap) {
    return EINVAL;
  }

  _a0_map_bucket_t bkt;
  do {
    A0_RETURN_ERR_ON_ERR(_a0_map_bucket(iter->_map, iter->_idx++, &bkt));
  } while (iter->_idx <= iter->_map->_cap && !*bkt.off);

  if (iter->_idx > iter->_map->_cap) {
    return EINVAL;
  }

  *key = bkt.key;
  *val = bkt.val;
  return A0_OK;
}
