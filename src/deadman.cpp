#include <a0/deadman.h>
#include <a0/deadman.hpp>
#include <a0/err.h>
#include <a0/mtx.h>
#include <a0/time.hpp>

#include <memory>
#include <string>

#include "c_wrap.hpp"

namespace a0 {

namespace {

a0_err_t ignore_owner_died(a0_err_t err) {
  if (a0_mtx_previous_owner_died(err)) {
    return A0_OK;
  }
  return err;
}

}  // namespace

Deadman::Deadman(DeadmanTopic topic) {
  set_c(
      &c,
      [&](a0_deadman_t* c) {
        a0_deadman_topic_t c_topic{topic.name.c_str()};
        return a0_deadman_init(c, c_topic);
      },
      a0_deadman_close);
}

void Deadman::take() {
  CHECK_C;
  check(ignore_owner_died(a0_deadman_take(&*c)));
}

bool Deadman::try_take() {
  CHECK_C;
  return a0_mtx_lock_successful(a0_deadman_trytake(&*c));
}

void Deadman::take(TimeMono timeout) {
  CHECK_C;
  check(ignore_owner_died(a0_deadman_timedtake(&*c, &*timeout.c)));
}

void Deadman::release() {
  CHECK_C;
  check(a0_deadman_release(&*c));
}

uint64_t Deadman::wait_taken() {
  CHECK_C;
  uint64_t tkn;
  check(ignore_owner_died(a0_deadman_wait_taken(&*c, &tkn)));
  return tkn;
}

uint64_t Deadman::wait_taken(TimeMono timeout) {
  CHECK_C;
  uint64_t tkn;
  check(ignore_owner_died(a0_deadman_timedwait_taken(&*c, &*timeout.c, &tkn)));
  return tkn;
}

void Deadman::wait_released(uint64_t tkn) {
  CHECK_C;
  check(ignore_owner_died(a0_deadman_wait_released(&*c, tkn)));
}

void Deadman::wait_released(uint64_t tkn, TimeMono timeout) {
  CHECK_C;
  check(ignore_owner_died(a0_deadman_timedwait_released(&*c, &*timeout.c, tkn)));
}

Deadman::State Deadman::state() {
  CHECK_C;
  a0_deadman_state_t state;
  check(ignore_owner_died(a0_deadman_state(&*c, &state)));
  return State{state.is_taken, state.is_owner, state.tkn};
}

}  // namespace a0
