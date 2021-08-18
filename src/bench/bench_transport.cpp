#define PICOBENCH_STD_FUNCTION_BENCHMARKS
#define PICOBENCH_IMPLEMENT

#include <a0.h>
#include <picobench/picobench.hpp>

static const char BENCH_FILE[] = "bench.a0";

template <typename T>
A0_STATIC_INLINE void use(const T& t) {
  asm volatile(""
               :
               : "r,m"(t)
               : "memory");
}

struct BenchFixture {
  BenchFixture() {
    a0_file_remove(BENCH_FILE);
    a0_file_open(BENCH_FILE, nullptr, &file);

    a0_transport_init(&transport, file.arena);
  }

  ~BenchFixture() {
    a0_file_close(&file);
    a0_file_remove(BENCH_FILE);
  }

  a0_file_t file;
  a0_transport_t transport;
};

using bench_fn_t = std::function<void(picobench::state&)>;

bench_fn_t bench_memcpy(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    std::string src(msg_size, 0);
    char* dst = (char*)malloc(msg_size);
    for (auto&& _ : s) {
      use(_);
      memcpy(dst, src.data(), msg_size);
    }
    free(dst);
  };
}

bench_fn_t bench_memcpy_slots(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    int slots = A0_FILE_OPTIONS_DEFAULT.create_options.size / msg_size;
    char** array = (char**)malloc(slots * sizeof(char*));
    for (int i = 0; i < slots; i++) {
      array[i] = (char*)malloc(msg_size);
    }
    std::string src(msg_size, 0);
    int slot = 0;
    for (auto&& _ : s) {
      use(_);
      memcpy(array[slot], src.data(), msg_size);
      slot = (slot + 1) % slots;
    }
    for (int i = 0; i < slots; i++) {
      free(array[i]);
    }
    free(array);
  };
}

bench_fn_t bench_malloc(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    for (auto&& _ : s) {
      use(_);
      void* ptr = malloc(msg_size);
      use(ptr);
      free(ptr);
    }
  };
}

bench_fn_t bench_malloc_slots(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    int slots = A0_FILE_OPTIONS_DEFAULT.create_options.size / msg_size;
    char** array = (char**)malloc(slots * sizeof(char*));
    for (int i = 0; i < slots; i++) {
      array[i] = (char*)malloc(msg_size);
    }

    int slot = 0;
    for (auto&& _ : s) {
      use(_);
      free(array[slot]);
      array[slot] = (char*)malloc(msg_size);
      slot = (slot + 1) % slots;
    }

    for (int i = 0; i < slots; i++) {
      free(array[i]);
    }
    free(array);
  };
}

bench_fn_t bench_malloc_memcpy_slots(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    int slots = A0_FILE_OPTIONS_DEFAULT.create_options.size / msg_size;
    char** array = (char**)malloc(slots * sizeof(char*));
    for (int i = 0; i < slots; i++) {
      array[i] = (char*)malloc(msg_size);
    }
    std::string src(msg_size, 0);

    int slot = 0;
    for (auto&& _ : s) {
      use(_);
      free(array[slot]);
      array[slot] = (char*)malloc(msg_size);
      memcpy(array[slot], src.data(), msg_size);
      slot = (slot + 1) % slots;
    }

    for (int i = 0; i < slots; i++) {
      free(array[i]);
    }
    free(array);
  };
}

bench_fn_t bench_a0_alloc(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    a0_locked_transport_t lk;
    a0_transport_lock(&fixture.transport, &lk);
    for (auto&& _ : s) {
      use(_);
      a0_transport_frame_t frame;
      a0_transport_alloc(lk, msg_size, &frame);
    }
    a0_transport_unlock(lk);
  };
}

bench_fn_t bench_a0_alloc_memcpy(int msg_size) {
  return [msg_size](picobench::state& s) {
    BenchFixture fixture;
    (void)fixture;

    std::string src(msg_size, 0);

    a0_locked_transport_t lk;
    a0_transport_lock(&fixture.transport, &lk);
    for (auto&& _ : s) {
      use(_);
      a0_transport_frame_t frame;
      a0_transport_alloc(lk, msg_size, &frame);
      memcpy(frame.data, src.data(), msg_size);
    }
    a0_transport_unlock(lk);
  };
}

int main() {
  struct suite {
    std::string name;
    int msg_size;
    int iter;
  };
  std::vector<suite> suites;
  suites.push_back({"64B msgs", 64, (int)2e7});
  suites.push_back({"1kB msgs", 1024, (int)1e7});
  suites.push_back({"10kB msgs", 10 * 1024, (int)2e6});
  suites.push_back({"1MB msgs", 1024 * 1024, (int)1e4});
  suites.push_back({"4MB msgs", 4 * 1024 * 1024, (int)2e3});

  for (auto&& suite : suites) {
    picobench::runner r;

    auto malloc_group = suite.name + " : malloc compare";
    r.set_suite(malloc_group.c_str());
    r.add_benchmark("malloc", bench_malloc(suite.msg_size)).iterations({suite.iter});
    r.add_benchmark("malloc_slots", bench_malloc_slots(suite.msg_size)).iterations({suite.iter});
    r.add_benchmark("a0_alloc", bench_a0_alloc(suite.msg_size)).iterations({suite.iter});

    auto memcpy_group = suite.name + " : memcpy compare";
    r.set_suite(memcpy_group.c_str());
    r.add_benchmark("memcpy", bench_memcpy(suite.msg_size)).iterations({suite.iter});
    r.add_benchmark("memcpy_slots", bench_memcpy_slots(suite.msg_size)).iterations({suite.iter});
    r.add_benchmark("malloc_memcpy_slots", bench_malloc_memcpy_slots(suite.msg_size))
        .iterations({suite.iter});
    r.add_benchmark("a0_alloc_memcpy", bench_a0_alloc_memcpy(suite.msg_size))
        .iterations({suite.iter});

    r.run();
  }
}
