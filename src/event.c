#include <a0/err.h>
#include <a0/event.h>
#include <a0/mtx.h>
#include <a0/time.h>

#include <stdbool.h>

a0_err_t a0_event_is_set(a0_event_t* e, bool* out) {
  a0_err_t err = a0_mtx_lock(&e->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }
  *out = e->_val;
  a0_mtx_unlock(&e->_mtx);
  return A0_OK;
}

a0_err_t a0_event_set(a0_event_t* e) {
  a0_err_t err = a0_mtx_lock(&e->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }
  e->_val = true;
  a0_cnd_broadcast(&e->_cnd, &e->_mtx);
  a0_mtx_unlock(&e->_mtx);
  return A0_OK;
}

a0_err_t a0_event_wait(a0_event_t* e) {
  return a0_event_timedwait(e, A0_TIMEOUT_NEVER);
}

a0_err_t a0_event_timedwait(a0_event_t* e, a0_time_mono_t* timeout) {
  a0_err_t err = a0_mtx_lock(&e->_mtx);
  if (!a0_mtx_lock_successful(err)) {
    return err;
  }
  err = A0_OK;

  while (!err && !e->_val) {
    err = a0_cnd_timedwait(&e->_cnd, &e->_mtx, timeout);
  }
  a0_mtx_unlock(&e->_mtx);
  return err;
}
