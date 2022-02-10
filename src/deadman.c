#include <a0/arena.h>
#include <a0/buf.h>
#include <a0/deadman_mtx.h>
#include <a0/deadman.h>
#include <a0/env.h>
#include <a0/err.h>
#include <a0/file.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/time.h>
#include <a0/topic.h>

#include <stdbool.h>
#include <stdint.h>

#include "err_macro.h"

A0_STATIC_INLINE
a0_err_t a0_deadman_topic_open(a0_deadman_topic_t topic, a0_file_t* file) {
  a0_file_options_t opts = A0_FILE_OPTIONS_DEFAULT;
  opts.create_options.size = sizeof(a0_deadman_mtx_t);
  return a0_topic_open(a0_env_topic_tmpl_deadman(), topic.name, &opts, file);
}

a0_err_t a0_deadman_init(a0_deadman_t* d, a0_deadman_topic_t topic) {
  A0_RETURN_ERR_ON_ERR(a0_deadman_topic_open(topic, &d->_file));
  d->_deadman_mtx = (a0_deadman_mtx_t*)d->_file.arena.buf.data;
  d->_is_owner = false;
  return A0_OK;
}

a0_err_t a0_deadman_close(a0_deadman_t* d) {
  a0_deadman_release(d);
  return a0_file_close(&d->_file);
}

a0_err_t a0_deadman_take(a0_deadman_t* d) {
  a0_err_t err = a0_deadman_mtx_lock(d->_deadman_mtx);
  d->_is_owner = a0_mtx_lock_successful(err);
  return err;
}

a0_err_t a0_deadman_trytake(a0_deadman_t* d) {
  a0_err_t err = a0_deadman_mtx_trylock(d->_deadman_mtx);
  d->_is_owner = a0_mtx_lock_successful(err);
  return err;
}

a0_err_t a0_deadman_timedtake(a0_deadman_t* d, a0_time_mono_t* timeout) {
  a0_err_t err = a0_deadman_mtx_timedlock(d->_deadman_mtx, timeout);
  d->_is_owner = a0_mtx_lock_successful(err);
  return err;
}

a0_err_t a0_deadman_release(a0_deadman_t* d) {
  a0_err_t err = A0_OK;
  if (d->_is_owner) {
    err = a0_deadman_mtx_unlock(d->_deadman_mtx);
    d->_is_owner = false;
  }
  return err;
}

a0_err_t a0_deadman_wait_taken(a0_deadman_t* d, uint64_t* out_tkn) {
  return a0_deadman_mtx_wait_locked(d->_deadman_mtx, out_tkn);
}

a0_err_t a0_deadman_timedwait_taken(a0_deadman_t* d, a0_time_mono_t* timeout, uint64_t* out_tkn) {
  return a0_deadman_mtx_timedwait_locked(d->_deadman_mtx, timeout, out_tkn);
}

a0_err_t a0_deadman_wait_released(a0_deadman_t* d, uint64_t tkn) {
  return a0_deadman_mtx_wait_unlocked(d->_deadman_mtx, tkn);
}

a0_err_t a0_deadman_timedwait_released(a0_deadman_t* d, a0_time_mono_t* timeout, uint64_t tkn) {
  return a0_deadman_mtx_timedwait_unlocked(d->_deadman_mtx, timeout, tkn);
}

a0_err_t a0_deadman_state(a0_deadman_t* d, bool* out_istaken, uint64_t* out_tkn) {
  return a0_deadman_mtx_state(d->_deadman_mtx, out_istaken, out_tkn);
}
