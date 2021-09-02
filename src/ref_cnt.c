#include "ref_cnt.h"

#include <a0/callback.h>
#include <a0/compare.h>
#include <a0/empty.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/map.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#include "err_macro.h"
#include "once.h"

typedef struct a0_counters_s {
  a0_map_t map;
  pthread_mutex_t mu;
  pthread_once_t init_flag;
} a0_counters_t;

A0_STATIC_INLINE
void _global_counters_init(void* user_data) {
  a0_counters_t* cnts = (a0_counters_t*)user_data;
  a0_map_init(
      &cnts->map,
      sizeof(void*),
      sizeof(size_t),
      A0_HASH_PTR,
      A0_COMPARE_PTR);
  pthread_mutex_init(&cnts->mu, NULL);
}

A0_STATIC_INLINE
a0_counters_t* _global_counters() {
  static a0_counters_t counters = A0_EMPTY;
  a0_callback_t init_fn = {
      .user_data = &counters,
      .fn = _global_counters_init,
  };
  a0_once(&counters.init_flag, init_fn);
  return &counters;
}

A0_STATIC_INLINE
a0_err_t _a0_ref_cnt_inc_locked(a0_map_t* map, void* key, size_t* out_cnt) {
  size_t unused_cnt;
  if (!out_cnt) {
    out_cnt = &unused_cnt;
  }

  bool has_key;
  A0_RETURN_ERR_ON_ERR(a0_map_has(map, &key, &has_key));
  if (!has_key) {
    size_t init_val = 0;
    A0_RETURN_ERR_ON_ERR(a0_map_put(map, &key, &init_val));
  }
  size_t* cnt;
  A0_RETURN_ERR_ON_ERR(a0_map_get(map, &key, (void**)&cnt));
  (*cnt)++;
  *out_cnt = *cnt;
  return A0_OK;
}

a0_err_t a0_ref_cnt_inc(void* key, size_t* out_cnt) {
  a0_counters_t* cnts = _global_counters();
  pthread_mutex_lock(&cnts->mu);
  a0_err_t err = _a0_ref_cnt_inc_locked(&cnts->map, key, out_cnt);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}

A0_STATIC_INLINE
a0_err_t _a0_ref_cnt_dec_locked(a0_map_t* map, void* key, size_t* out_cnt) {
  size_t unused_cnt;
  if (!out_cnt) {
    out_cnt = &unused_cnt;
  }

  bool has_key;
  A0_RETURN_ERR_ON_ERR(a0_map_has(map, &key, &has_key));
  if (!has_key) {
    return A0_ERRCODE_NOT_FOUND;
  }

  size_t* cnt;
  A0_RETURN_ERR_ON_ERR(a0_map_get(map, &key, (void**)&cnt));
  (*cnt)--;
  *out_cnt = *cnt;

  if (*cnt == 0) {
    A0_RETURN_ERR_ON_ERR(a0_map_del(map, &key));
  }
  return A0_OK;
}

a0_err_t a0_ref_cnt_dec(void* key, size_t* out_cnt) {
  a0_counters_t* cnts = _global_counters();
  pthread_mutex_lock(&cnts->mu);
  a0_err_t err = _a0_ref_cnt_dec_locked(&cnts->map, key, out_cnt);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}

A0_STATIC_INLINE
a0_err_t _a0_ref_cnt_get_locked(a0_map_t* map, void* key, size_t* out) {
  *out = 0;

  bool has_key;
  A0_RETURN_ERR_ON_ERR(a0_map_has(map, &key, &has_key));
  if (has_key) {
    size_t* cnt;
    A0_RETURN_ERR_ON_ERR(a0_map_get(map, &key, (void**)&cnt));
    *out = *cnt;
  }

  return A0_OK;
}

a0_err_t a0_ref_cnt_get(void* key, size_t* out) {
  a0_counters_t* cnts = _global_counters();
  pthread_mutex_lock(&cnts->mu);
  a0_err_t err = _a0_ref_cnt_get_locked(&cnts->map, key, out);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}
