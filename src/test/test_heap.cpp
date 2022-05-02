#include <doctest.h>

#include <a0/heap.h>

#include <random>
#include <queue>

#include "src/test_util.hpp"

struct obj_t {
  int weight;
  int content;

  // Min heap.
  bool operator<(const obj_t& rhs) const {
    return weight > rhs.weight;
  }

  static a0_compare_t make_compare() {
    return {
      .user_data = nullptr,
      .fn = [](void*, const void* lhs, const void* rhs, int* out) {
        *out = ((obj_t*)lhs)->weight - ((obj_t*)rhs)->weight;
        return A0_OK;
      },
    };
  }
};

TEST_CASE("heap] basic") {
  a0_heap_t heap;
  REQUIRE_OK(a0_heap_init(&heap, sizeof(obj_t), obj_t::make_compare()));

  size_t size;
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 0);

  obj_t obj = {1, 2};
  REQUIRE_OK(a0_heap_put(&heap, &obj));
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 1);

  obj = {2, 4};
  REQUIRE_OK(a0_heap_put(&heap, &obj));
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 2);

  obj = {3, 6};
  REQUIRE_OK(a0_heap_put(&heap, &obj));
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 3);

  const obj_t* obj_ptr;
  REQUIRE_OK(a0_heap_top(&heap, (const void**)&obj_ptr));
  REQUIRE(obj_ptr->weight == 1);
  REQUIRE(obj_ptr->content == 2);

  REQUIRE_OK(a0_heap_pop(&heap, nullptr));
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 2);

  REQUIRE_OK(a0_heap_pop(&heap, (void*)&obj));
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == 1);
  REQUIRE(obj.weight == 2);
  REQUIRE(obj.content == 4);

  REQUIRE_OK(a0_heap_pop(&heap, nullptr));
  REQUIRE(a0_heap_pop(&heap, nullptr) == EINVAL);

  REQUIRE_OK(a0_heap_close(&heap));
}

TEST_CASE("heap] fuzz") {
  a0_heap_t heap;
  REQUIRE_OK(a0_heap_init(
      &heap, sizeof(obj_t), obj_t::make_compare()));

  std::default_random_engine rng;
  rng.seed(0);
  std::uniform_int_distribution<int> rand_int(-1000, 1000);
  
  auto dur = std::chrono::nanoseconds(0);
  auto ref_dur = std::chrono::nanoseconds(0);

  std::priority_queue<obj_t> ref_heap;
  for (size_t i = 0; i < 100000000; i++) {
    switch (rng() % 2) {
      case 0: {
        if (ref_heap.size() > 10) {
          continue;
        }
        obj_t new_obj{rand_int(rng), rand_int(rng)};

        auto start = std::chrono::steady_clock::now();
        ref_heap.push(new_obj);
        auto end = std::chrono::steady_clock::now();
        ref_dur += end - start;

        start = std::chrono::steady_clock::now();
        auto err = a0_heap_put(&heap, &new_obj);
        end = std::chrono::steady_clock::now();
        dur += end - start;

        REQUIRE_OK(err);
        break;
      }
      case 1: {
        if (ref_heap.empty()) {
          continue;
        }

        auto start = std::chrono::steady_clock::now();
        obj_t got_obj;
        auto err = a0_heap_pop(&heap, &got_obj);
        auto end = std::chrono::steady_clock::now();
        // dur += end - start;

        start = std::chrono::steady_clock::now();
        obj_t ref_obj = ref_heap.top();
        ref_heap.pop();
        end = std::chrono::steady_clock::now();
        // ref_dur += end - start;

        REQUIRE_OK(err);
        REQUIRE(got_obj.weight == ref_obj.weight);
        // Weights can be duplicated, so content might not match.
        // REQUIRE(got_obj.content == ref_obj.content);
        break;
      }
    }
  }
  fprintf(stderr, "    dur=%ld\n", dur.count());
  fprintf(stderr, "ref_dur=%ld\n", ref_dur.count());
  fprintf(stderr, "faster=%lf\n", ((double)ref_dur.count() - dur.count()) / ref_dur.count());
  fprintf(stderr, "    size=%ld\n", sizeof(obj_t));

  size_t size;
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == ref_heap.size());

  REQUIRE_OK(a0_heap_close(&heap));
}
