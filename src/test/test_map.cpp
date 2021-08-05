#include <doctest.h>

#include <a0/map.h>

#include <random>
#include <unordered_map>

#include "src/test_util.hpp"

struct MapFixture {
  using map_key_t = uint32_t;
  using map_val_t = uint64_t;
  using ref_map_t = std::unordered_map<map_key_t, map_val_t>;

  a0_hash_t make_hash() {
    return (a0_hash_t){
        .user_data = nullptr,
        .fn = [](void* user_data, const void* key, size_t* out) {
          (void)user_data;
          *out = *(map_key_t*)key;
          return A0_OK;
        },
    };
  }

  a0_compare_t make_compare() {
    return (a0_compare_t){
        .user_data = nullptr,
        .fn = [](void* user_data, const void* lhs, const void* rhs, int* out) {
          (void)user_data;
          *out = *(map_key_t*)lhs - *(map_key_t*)rhs;
          return A0_OK;
        },
    };
  }
};

TEST_CASE_FIXTURE(MapFixture, "map] basic") {
  a0_map_t map;
  REQUIRE_OK(a0_map_init(
      &map, sizeof(map_key_t), sizeof(map_val_t), make_hash(), make_compare()));

  size_t size;
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == 0);

  map_key_t key = 1;
  map_val_t val = 1;
  REQUIRE_OK(a0_map_put(&map, &key, &val));
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == 1);

  key = 2;
  val = 4;
  REQUIRE_OK(a0_map_put(&map, &key, &val));
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == 2);

  key = 4;
  val = 8;
  REQUIRE_OK(a0_map_put(&map, &key, &val));
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == 3);

  bool found;
  key = 2;
  REQUIRE_OK(a0_map_has(&map, &key, &found));
  REQUIRE(found);

  const map_key_t* key_ptr = nullptr;
  map_val_t* val_ptr = nullptr;
  REQUIRE_OK(a0_map_get(&map, &key, (void**)&val_ptr));
  REQUIRE(*val_ptr == 4);

  key = 3;
  REQUIRE_OK(a0_map_has(&map, &key, &found));
  REQUIRE(!found);
  REQUIRE(a0_map_get(&map, &key, (void**)&val_ptr) == EINVAL);

  key = 2;
  REQUIRE_OK(a0_map_del(&map, &key));
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == 2);
  REQUIRE_OK(a0_map_has(&map, &key, &found));
  REQUIRE(!found);

  ref_map_t got;
  a0_map_iterator_t iter;
  REQUIRE_OK(a0_map_iterator_init(&iter, &map));
  while (a0_map_iterator_next(&iter, (const void**)&key_ptr, (void**)&val_ptr) == A0_OK) {
    got[*key_ptr] = *val_ptr;
  }
  REQUIRE(got == ref_map_t{{1, 1}, {4, 8}});

  REQUIRE_OK(a0_map_close(&map));
}

TEST_CASE_FIXTURE(MapFixture, "map] fuzz") {
  a0_map_t map;
  REQUIRE_OK(a0_map_init(
      &map, sizeof(map_key_t), sizeof(map_val_t), make_hash(), make_compare()));

  std::mt19937_64 rng(std::random_device{}());

  ref_map_t ref_map;
  for (size_t i = 0; i < 1000000; i++) {
    switch (rng() % 3) {
      case 0: {
        if (ref_map.size() > 3) {
          continue;
        }
        map_key_t key = rng();
        map_val_t value = rng();
        REQUIRE_OK(a0_map_put(&map, &key, &value));
        ref_map[key] = value;
        break;
      }
      case 1: {
        if (ref_map.empty()) {
          continue;
        }
        auto it = std::next(std::begin(ref_map), rng() % ref_map.size());
        map_key_t key = it->first;
        REQUIRE_OK(a0_map_del(&map, &key));
        ref_map.erase(it);
        break;
      }
      case 2: {
        if (ref_map.empty()) {
          continue;
        }
        auto it = std::next(std::begin(ref_map), rng() % ref_map.size());
        map_key_t key = it->first;
        map_val_t value = rng();
        REQUIRE_OK(a0_map_put(&map, &key, &value));
        ref_map[key] = value;
        break;
      }
    }
  }

  size_t size;
  REQUIRE_OK(a0_map_size(&map, &size));
  REQUIRE(size == ref_map.size());

  for (auto&& [key, value] : ref_map) {
    bool contains;
    REQUIRE_OK(a0_map_has(&map, &key, &contains));
    REQUIRE(contains);
    map_val_t* value_ptr = nullptr;
    REQUIRE_OK(a0_map_get(&map, &key, (void**)&value_ptr));
    REQUIRE(*value_ptr == value);
  }

  REQUIRE_OK(a0_map_close(&map));
}