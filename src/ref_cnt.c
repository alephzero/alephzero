#include "ref_cnt.h"

#include <a0/compare.h>
#include <a0/err.h>
#include <a0/inline.h>
#include <a0/map.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#include "atomic.h"
#include "empty.h"
#include "err_util.h"

typedef enum a0_init_status_e {
  A0_INIT_NOT_STARTED,
  A0_INIT_STARTED,
  A0_INIT_DONE,
} a0_init_status_t;

typedef struct a0_counters_s {
  a0_map_t map;
  pthread_mutex_t mu;
  a0_init_status_t init_status;
} a0_counters_t;

A0_STATIC_INLINE
a0_counters_t* global_counters() {
  static a0_counters_t counters = A0_EMPTY;

  if (a0_cas(&counters.init_status, A0_INIT_NOT_STARTED, A0_INIT_STARTED)) {
    a0_map_init(
        &counters.map,
        sizeof(void*),
        sizeof(size_t),
        A0_HASH_PTR,
        A0_COMPARE_PTR);
    pthread_mutex_init(&counters.mu, NULL);
    a0_atomic_store(&counters.init_status, A0_INIT_DONE);
  } else {
    while (a0_atomic_load(&counters.init_status) != A0_INIT_DONE) {
    }
  }

  return &counters;
}

A0_STATIC_INLINE
errno_t a0_ref_cnt_inc_locked(a0_map_t* map, void* key, size_t* out_cnt) {
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

errno_t a0_ref_cnt_inc(void* key, size_t* out_cnt) {
  a0_counters_t* cnts = global_counters();
  pthread_mutex_lock(&cnts->mu);
  errno_t err = a0_ref_cnt_inc_locked(&cnts->map, key, out_cnt);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}

A0_STATIC_INLINE
errno_t a0_ref_cnt_dec_locked(a0_map_t* map, void* key, size_t* out_cnt) {
  size_t unused_cnt;
  if (!out_cnt) {
    out_cnt = &unused_cnt;
  }

  bool has_key;
  A0_RETURN_ERR_ON_ERR(a0_map_has(map, &key, &has_key));
  if (!has_key) {
    return EINVAL;
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

errno_t a0_ref_cnt_dec(void* key, size_t* out_cnt) {
  a0_counters_t* cnts = global_counters();
  pthread_mutex_lock(&cnts->mu);
  errno_t err = a0_ref_cnt_dec_locked(&cnts->map, key, out_cnt);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}

A0_STATIC_INLINE
errno_t a0_ref_cnt_get_locked(a0_map_t* map, void* key, size_t* out) {
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

errno_t a0_ref_cnt_get(void* key, size_t* out) {
  a0_counters_t* cnts = global_counters();
  pthread_mutex_lock(&cnts->mu);
  errno_t err = a0_ref_cnt_get_locked(&cnts->map, key, out);
  pthread_mutex_unlock(&cnts->mu);
  return err;
}
