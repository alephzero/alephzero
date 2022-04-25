#include <a0/empty.h>
#include <a0/err.h>
#include <a0/latch.h>
#include <a0/mtx.h>

#include <stdbool.h>
#include <stdint.h>

a0_err_t a0_latch_init(a0_latch_t* l, int32_t init_val) {
  *l = (a0_latch_t)A0_EMPTY;
  l->_val = init_val;
  return A0_OK;
}

a0_err_t a0_latch_count_down(a0_latch_t* l, int32_t update) {
  a0_err_t err = a0_mtx_lock(&l->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }

  l->_val -= update;
  if (l->_val <= 0) {
    a0_cnd_broadcast(&l->_cnd, &l->_mtx);
  }

  a0_mtx_unlock(&l->_mtx);
  return A0_OK;
}

a0_err_t a0_latch_try_wait(a0_latch_t* l, bool* out) {
  a0_err_t err = a0_mtx_lock(&l->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }

  *out = l->_val <= 0;

  a0_mtx_unlock(&l->_mtx);
  return A0_OK;
}

a0_err_t a0_latch_wait(a0_latch_t* l) {
  return a0_latch_arrive_and_wait(l, 0);
}

a0_err_t a0_latch_arrive_and_wait(a0_latch_t* l, int32_t update) {
  a0_err_t err = a0_mtx_lock(&l->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }

  l->_val -= update;
  if (l->_val <= 0) {
    a0_cnd_broadcast(&l->_cnd, &l->_mtx);
  }
  while (l->_val > 0) {
    a0_cnd_wait(&l->_cnd, &l->_mtx);
  }

  a0_mtx_unlock(&l->_mtx);
  return A0_OK;
}
