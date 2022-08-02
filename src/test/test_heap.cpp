#include <doctest.h>

#include <a0/cmp.h>
#include <a0/err.h>
#include <a0/heap.h>

#include <cstdlib>
#include <queue>

#include "src/test_util.hpp"

struct obj_t {
  int weight;
  int content;

  // Min heap.
  bool operator<(const obj_t& rhs) const {
    return weight > rhs.weight;
  }

  static a0_cmp_t make_compare() {
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
  REQUIRE(a0_heap_pop(&heap, nullptr) == A0_ERR_AGAIN);

  REQUIRE_OK(a0_heap_close(&heap));
}

TEST_CASE("heap] fuzz") {
  a0_heap_t heap;
  REQUIRE_OK(a0_heap_init(&heap, sizeof(obj_t), obj_t::make_compare()));

  std::priority_queue<obj_t> ref_heap;
  for (size_t i = 0; i < 1000000; i++) {
    switch (rand() % 2) {
      case 0: {
        if (ref_heap.size() > 10) {
          continue;
        }
        obj_t new_obj{rand(), rand()};

        ref_heap.push(new_obj);
        REQUIRE_OK(a0_heap_put(&heap, &new_obj));
        break;
      }
      case 1: {
        if (ref_heap.empty()) {
          continue;
        }

        obj_t got_obj;
        REQUIRE_OK(a0_heap_pop(&heap, &got_obj));

        obj_t ref_obj = ref_heap.top();
        ref_heap.pop();

        REQUIRE(got_obj.weight == ref_obj.weight);
        // Weights can be duplicated, so content might not match.
        // REQUIRE(got_obj.content == ref_obj.content);
        break;
      }
    }
  }

  size_t size;
  REQUIRE_OK(a0_heap_size(&heap, &size));
  REQUIRE(size == ref_heap.size());

  REQUIRE_OK(a0_heap_close(&heap));
}
