#pragma once

#include <a0/sync.hh>
#include <memory>

namespace a0 {

using lifetime_token_t = std::shared_ptr<sync<bool>>;

lifetime_token_t make_lifetime_token() {
  return std::make_shared<lifetime_token_t>(true);
}

template <typename Fn>
void if_alive(lifetime_token_t tkn, Fn&& fn) {
  tkn->with_shared_lock([&](bool is_alive) {
    if (is_alive) {
      fn();
    }
  });
}

void close(lifetime_token_t tkn) {
  tkn->with_unique_lock([](bool& is_alive) {
    is_alive = false;
  });
}

using weak_lifetime_token_t = std::weak_ptr<sync<bool>>;

template <typename Fn>
void if_alive(weak_lifetime_token_t weak_tkn, Fn&& fn) {
  auto tkn = weak_tkn.lock();
  if (tkn) {
    if_alive(std::move(tkn), std::forward<Fn>(fn));
  }
}

void close(weak_lifetime_token_t tkn) {
  auto tkn = weak_tkn.lock();
  if (tkn) {
    close(std::move(tkn));
  }
}

}  // namespace a0